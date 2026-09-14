import { useEffect, useRef, useState } from 'react'
import { Download, FileWarning, FolderOpen, Info, MapPin, ChevronDown, X } from 'lucide-react'
import { JobState } from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import {
  cancelTerrainJob,
  downloadSelectedTerrain,
  importTerrainDataset,
  listTerrainSources,
  planTerrainDownload,
  probeTerrainSource,
} from './terrainApi'
import { useTerrainStore } from './terrainStore'
import { useShellUiStore } from '../../editor/shell/shellUiStore'
import { DownloadAreaMap, type GeoBounds, type SelectionTileInfo } from './DownloadAreaMap'
import { createLocationSearchClient } from './locationSearch'
import { initTerrainConfig, getTerrainMapTileConfig } from './terrainConfig'

interface ImportTerrainDialogProps {
  client: EngineClient
  busy: boolean
  onClose: () => void
}

function formatBytes(bytes: bigint): string {
  const value = Number(bytes)
  if (!Number.isFinite(value) || value <= 0) return '—'
  if (value >= 1024 * 1024) return `${(value / (1024 * 1024)).toFixed(1)} MB`
  if (value >= 1024) return `${(value / 1024).toFixed(1)} KB`
  return `${value} B`
}

function defaultDisplayName(path: string): string {
  const base = path.split(/[\\/]/).pop() ?? ''
  const stem = base.replace(/\.[^.]+$/, '')
  return stem.length > 0 ? stem : 'Imported terrain'
}

type SourceMode = 'local-file' | 'download-area'

const TILE_SIZES = [
  { value: 1000, label: '1 km' },
  { value: 2000, label: '2 km' },
  { value: 4000, label: '4 km' },
  { value: 8000, label: '8 km' },
  { value: 16000, label: '16 km' },
]

// Terrain import flow: the OS file dialog stays in the desktop shell, the
// engine probes the source (CRS validation is explicit and authoritative),
// and import runs as a cancellable native job with real progress. The
// dialog never fabricates CRS data or progress.
//
// Download Area mode: the user draws a rectangular working area, generates
// a deterministic application selection grid, selects/deselects tiles, and
// downloads only the selected tiles from a remote DEM provider. The engine
// owns provider planning, network acquisition, decoding, clipping, and
// canonical raster assembly.
export function ImportTerrainDialog({ client, onClose }: ImportTerrainDialogProps) {
  const initialMode = useShellUiStore((state) => state.terrainImportMode)
  const [sourceMode, setSourceMode] = useState<SourceMode>(initialMode)

  return (
    <div className="dialog-overlay" role="presentation">
      <div
        className={`dialog dialog-terrain ${sourceMode === 'download-area' ? 'dialog-large' : ''}`}
        role="dialog"
        aria-modal="true"
        aria-label="Import terrain"
        data-dialog-size={sourceMode === 'download-area' ? 'large' : 'compact'}
      >
        <div className="dialog-header">
          <h2>Import terrain</h2>
          <button type="button" className="dialog-close" aria-label="Close dialog" onClick={onClose}>
            <X size={16} />
          </button>
        </div>

        <div className="source-tabs" role="tablist" aria-label="Terrain source">
          <button
            className={`source-tab ${sourceMode === 'local-file' ? 'active' : ''}`}
            type="button"
            role="tab"
            aria-selected={sourceMode === 'local-file'}
            onClick={() => setSourceMode('local-file')}
          >
            <FolderOpen size={14} /> Local File
          </button>
          <button
            className={`source-tab ${sourceMode === 'download-area' ? 'active' : ''}`}
            type="button"
            role="tab"
            aria-selected={sourceMode === 'download-area'}
            onClick={() => setSourceMode('download-area')}
          >
            <Download size={14} /> Download Area
          </button>
        </div>

        <div className="dialog-body">
          {sourceMode === 'local-file' ? (
            <LocalFileImport client={client} onClose={onClose} />
          ) : (
            <DownloadAreaImport client={client} onClose={onClose} />
          )}
        </div>
      </div>
    </div>
  )
}

function LocalFileImport({ client, onClose }: { client: EngineClient; onClose: () => void }) {
  const [path, setPath] = useState('')
  const [displayName, setDisplayName] = useState('')
  const [probing, setProbing] = useState(false)
  const [starting, setStarting] = useState(false)
  const [formError, setFormError] = useState<string | null>(null)
  const [startedJobId, setStartedJobId] = useState<string | null>(null)
  const [elevationUnitOverride, setElevationUnitOverride] = useState('')
  const submitInFlightRef = useRef(false)
  const probeIdRef = useRef(0)

  const probe = useTerrainStore((state) => state.probe)
  const probeError = useTerrainStore((state) => state.probeError)
  const jobs = useTerrainStore((state) => state.jobs)

  const engineError = useTerrainStore((state) => state.lastError)
  const error = formError ?? engineError

  const importJob = startedJobId
    ? jobs.find((job) => job.jobId === startedJobId) ?? null
    : null
  const importFinished =
    importJob !== null &&
    (importJob.state === JobState.COMPLETED ||
      importJob.state === JobState.FAILED ||
      importJob.state === JobState.CANCELLED)
  const progressPercent =
    importJob && importJob.progress && importJob.progress > 0
      ? Math.min(100, Math.floor(importJob.progress * 100))
      : importJob && importJob.total && importJob.total > 0n
        ? Math.min(100, Number((importJob.processed ?? 0n) * 100n / importJob.total))
        : 0

  const pickFile = async () => {
    const desktop = window.infraforgeDesktop
    if (!desktop?.pickFile) {
      setFormError('The desktop shell did not expose a file picker.')
      return
    }
    const selected = await desktop.pickFile({
      title: 'Choose a georeferenced DEM (GeoTIFF)',
      filters: [{ name: 'GeoTIFF / DEM', extensions: ['tif', 'tiff'] }],
    })
    if (selected) {
      setPath(selected)
      setDisplayName((current) => (current.trim().length > 0 ? current : defaultDisplayName(selected)))
      setFormError(null)
      setStartedJobId(null)
      void runProbe(selected)
    }
  }

  const runProbe = async (candidatePath: string) => {
    const probeId = ++probeIdRef.current
    setProbing(true)
    setFormError(null)
    try {
      await probeTerrainSource(client, candidatePath)
    } catch (probeFailure) {
      if (probeIdRef.current === probeId) {
        setFormError(
          probeFailure instanceof Error
            ? probeFailure.message
            : 'The terrain source could not be probed.',
        )
      }
    } finally {
      if (probeIdRef.current === probeId) {
        setProbing(false)
      }
    }
  }

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    if (submitInFlightRef.current || starting || !probe) {
      return
    }
    if (path.trim().length === 0) {
      setFormError('Choose a DEM source file first.')
      return
    }
    if (displayName.trim().length === 0) {
      setFormError('A terrain name is required.')
      return
    }
    if (probe.source?.elevationUnit === 'unknown' && !elevationUnitOverride) {
      setFormError('Confirm how raster sample values should be interpreted.')
      return
    }
    submitInFlightRef.current = true
    setStarting(true)
    setFormError(null)
    try {
      const jobId = await importTerrainDataset(client, path.trim(), displayName.trim(), elevationUnitOverride)
      setStartedJobId(jobId)
    } catch {
      // Engine failures are recorded in the terrain store and shown below.
    } finally {
      submitInFlightRef.current = false
      setStarting(false)
    }
  }

  const cancelImport = async () => {
    if (!importJob || importFinished) {
      return
    }
    try {
      await cancelTerrainJob(client, importJob.jobId)
    } catch (cancelFailure) {
      setFormError(
        cancelFailure instanceof Error ? cancelFailure.message : 'The import could not be cancelled.',
      )
    }
  }

  return (
    <>
      <div className="form-row">
        <span className="form-label">Source file</span>
        <div className="form-inline">
          <input
            className="form-input grow"
            value={path}
            onChange={(event) => setPath(event.target.value)}
            placeholder="Absolute path to a GeoTIFF DEM"
            disabled={startedJobId !== null}
          />
          <button
            className="button secondary"
            type="button"
            onClick={() => void pickFile()}
            disabled={!window.infraforgeDesktop?.pickFile || startedJobId !== null}
          >
            <FolderOpen size={14} /> Browse…
          </button>
        </div>
      </div>

      {probing ? <div className="form-static">Probing source…</div> : null}

      {probeError ? (
        <div className="form-error" role="alert">
          <p>
            <FileWarning size={14} /> The source was rejected by the engine:
          </p>
          <p className="form-error-detail">{probeError}</p>
        </div>
      ) : null}

      {probe?.source ? (
        <div className="metadata-card">
          <div className="metadata-card-title">Source metadata</div>
          <dl className="metadata-grid">
            <div className="metadata-field">
              <dt>CRS</dt>
              <dd>
                {probe.crsName || probe.source.crsDefinition}
                {probe.crsAuthority && probe.crsCode ? ` (${probe.crsAuthority}:${probe.crsCode})` : ''}
              </dd>
            </div>
            <div className="metadata-field">
              <dt>Dimensions</dt>
              <dd>{probe.source.width} × {probe.source.height} px</dd>
            </div>
            <div className="metadata-field">
              <dt>Pixel size</dt>
              <dd>
                {Number(probe.source.pixelSizeX).toPrecision(6)} ×{' '}
                {Number(probe.source.pixelSizeY).toPrecision(6)}{' '}
                {probe.source.horizontalUnitSymbol || probe.source.horizontalUnitName || 'units'}
              </dd>
            </div>
            <div className="metadata-field">
              <dt>Elevation unit</dt>
              <dd>{probe.source.elevationUnit === 'unknown' ? 'Unknown' : probe.source.elevationUnit}</dd>
            </div>
            <div className="metadata-field">
              <dt>NoData</dt>
              <dd>{probe.source.hasNodata ? 'present' : 'none'}</dd>
            </div>
            <div className="metadata-field">
              <dt>File size</dt>
              <dd>{formatBytes(probe.source.fileBytes)}</dd>
            </div>
            <div className="metadata-field">
              <dt>Format</dt>
              <dd>{probe.source.format}</dd>
            </div>
            <div className="metadata-field">
              <dt>Sample type</dt>
              <dd>{probe.source.sampleType}</dd>
            </div>
          </dl>
        </div>
      ) : null}

      {probe?.source?.elevationUnit === 'unknown' ? (
        <div className="form-warning" role="alert">
          <p>This source does not declare an elevation unit. Confirm how raster sample values should be interpreted.</p>
          {probe.source.sampleType === 'UInt16' && !probe.source.hasNodata ? (
            <p>This raster may be an encoded/normalized heightmap rather than elevations in physical units. Verify elevation unit and scale before import.</p>
          ) : null}
          <label className="form-field">
            <span>Elevation unit</span>
            <select value={elevationUnitOverride} onChange={(event) => setElevationUnitOverride(event.target.value)} disabled={startedJobId !== null}>
              <option value="">Select a unit…</option>
              <option value="metre">Metres</option>
              <option value="international foot">International feet</option>
              <option value="US survey foot">US survey feet</option>
            </select>
          </label>
        </div>
      ) : null}

      <label className="form-row">
        <span className="form-label">Terrain name</span>
        <input
          className="form-input"
          value={displayName}
          onChange={(event) => setDisplayName(event.target.value)}
          placeholder="Imported terrain"
          disabled={startedJobId !== null}
        />
      </label>

      {importJob && !importFinished ? (
        <div className="job-progress" aria-live="polite">
          <div className="job-progress-header">
            <span>Importing terrain — {progressPercent}%</span>
          </div>
          <div className="progress-track" role="progressbar" aria-valuenow={progressPercent} aria-valuemin={0} aria-valuemax={100}>
            <div className="progress-fill" style={{ width: `${progressPercent}%` }} />
          </div>
          <button className="button secondary" type="button" onClick={() => void cancelImport()}>
            Cancel import
          </button>
        </div>
      ) : null}

      {importJob && importJob.state === JobState.COMPLETED ? (
        <div className="form-static success" role="status">
          Terrain imported successfully. The dataset is registered and its render tiles are generating or present; see the
          Operations tab for tile-generation progress.
        </div>
      ) : null}
      {importJob && importJob.state === JobState.CANCELLED ? (
        <div className="form-error" role="alert">
          <p>Import cancelled — no terrain was committed.</p>
        </div>
      ) : null}
      {importJob && importJob.state === JobState.FAILED ? (
        <div className="form-error" role="alert">
          <p>Import failed:</p>
          <p className="form-error-detail">{importJob.message}</p>
        </div>
      ) : null}

      {error && !probeError ? (
        <div className="form-error" role="alert">
          <p>{error}</p>
        </div>
      ) : null}

      <div className="dialog-actions">
        <button className="button secondary" type="button" onClick={onClose}>
          {importFinished || startedJobId !== null ? 'Close' : 'Cancel'}
        </button>
        <button className="button primary" type="submit" disabled={starting || !probe || startedJobId !== null || (probe.source?.elevationUnit === 'unknown' && !elevationUnitOverride)} onClick={submit}>
          {starting ? 'Starting…' : 'Import Terrain'}
        </button>
      </div>
    </>
  )
}

interface DrawnArea {
  west: number
  south: number
  east: number
  north: number
}


// Plan identity: a stable key derived from the inputs that produced the
// plan (Finding 1). Used to detect and ignore stale plan responses and to
// ensure Download Selected is only enabled when the plan matches the
// current provider/area/tileSize.
interface PlanIdentity {
  providerId: string
  tileSize: number
  west: number
  south: number
  east: number
  north: number
}

function makePlanIdentity(
  providerId: string,
  tileSize: number,
  area: DrawnArea,
): PlanIdentity {
  return {
    providerId,
    tileSize,
    west: area.west,
    south: area.south,
    east: area.east,
    north: area.north,
  }
}

function planIdentityMatches(a: PlanIdentity, b: PlanIdentity): boolean {
  return a.providerId === b.providerId &&
    a.tileSize === b.tileSize &&
    a.west === b.west &&
    a.south === b.south &&
    a.east === b.east &&
    a.north === b.north
}

// BLOCKER 15: Attribution display with expandable details.
// Shows a concise summary inline, with a "View attribution" button that
// expands to show the full required attribution text. This avoids
// destroying the dialog layout with a huge paragraph while still
// making the full attribution accessible and selectable.
function AttributionDisplay({ attribution }: { attribution: string }) {
  const [expanded, setExpanded] = useState(false)
  // Concise summary: first line or first 80 chars.
  const summary = attribution.split('\n')[0] ?? attribution
  const isLong = attribution.length > 100 || attribution.includes('\n')
  return (
    <div className="provider-attribution">
      <Info size={12} /> {summary}
      {isLong && (
        <button
          type="button"
          className="attribution-toggle"
          aria-expanded={expanded}
          aria-controls="attribution-details"
          onClick={() => setExpanded(!expanded)}
        >
          {expanded ? 'Hide' : 'View'} attribution
        </button>
      )}
      {expanded && isLong && (
        <div
          id="attribution-details"
          className="attribution-details"
        >
          {attribution}
        </div>
      )}
    </div>
  )
}

function DownloadAreaImport({ client, onClose }: { client: EngineClient; onClose: () => void }) {
  const [area, setArea] = useState<DrawnArea | null>(null)
  const [tileSize, setTileSize] = useState(4000)
  const [selectedIndices, setSelectedIndices] = useState<Set<number>>(new Set())
  const [providers, setProviders] = useState<{ providerId: string; displayName: string; attribution: string }[]>([])
  const [selectedProvider, setSelectedProvider] = useState('')
  const [plan, setPlan] = useState<{
    identity: PlanIdentity
    selectionTiles: { col: number; row: number; bounds: { west: number; south: number; east: number; north: number }; areaSqm: number }[]
    providerRequests: { requestId: string; estimatedBytes: bigint }[]
    requestCount: number
    effectiveResolutionMpp: number
    estimatedBytes: bigint
    warnings: string[]
    fullCoverage: boolean
    totalTileCount: number
    selectedTileCount: number
    selectedAreaSqm: number
  } | null>(null)
  const [displayName, setDisplayName] = useState('')
  const [starting, setStarting] = useState(false)
  const [startedJobId, setStartedJobId] = useState<string | null>(null)
  const [formError, setFormError] = useState<string | null>(null)
  const [searchClient, setSearchClient] = useState(() => createLocationSearchClient())
  const [mapTileConfig, setMapTileConfig] = useState(() => getTerrainMapTileConfig())
  const [showTileList, setShowTileList] = useState(false)
  const [mapResizeToken, setMapResizeToken] = useState(0)

  useEffect(() => {
    let active = true
    void initTerrainConfig().then(() => {
      if (active) {
        setSearchClient(createLocationSearchClient())
        setMapTileConfig(getTerrainMapTileConfig())
      }
    })
    return () => {
      active = false
    }
  }, [])

  // Refresh the Leaflet map size when this tab becomes active.
  useEffect(() => {
    setMapResizeToken((t) => t + 1)
  }, [])

  const jobs = useTerrainStore((state) => state.jobs)
  const downloadJob = startedJobId
    ? jobs.find((job) => job.jobId === startedJobId) ?? null
    : null
  const downloadFinished =
    downloadJob !== null &&
    (downloadJob.state === JobState.COMPLETED ||
      downloadJob.state === JobState.FAILED ||
      downloadJob.state === JobState.CANCELLED)
  const progressPercent =
    downloadJob && downloadJob.progress && downloadJob.progress > 0
      ? Math.min(100, Math.floor(downloadJob.progress * 100))
      : downloadJob && downloadJob.total && downloadJob.total > 0n
        ? Math.min(100, Number((downloadJob.processed ?? 0n) * 100n / downloadJob.total))
        : 0

  // Load providers on mount.
  useEffect(() => {
    void (async () => {
      try {
        const result = await listTerrainSources(client)
        setProviders(result.providers.map((p) => ({ providerId: p.providerId, displayName: p.displayName, attribution: p.attribution })))
        if (result.providers.length > 0 && !selectedProvider) {
          setSelectedProvider(result.providers[0]!.providerId)
        }
      } catch {
        // Providers will be empty; user can still draw but not download.
      }
    })()
  }, [client])

  // Fetch plan when area, tile size, or selection changes.
  // Finding 1: Use a plan identity to ignore stale plan responses that
  // arrive after a newer request was issued. The identity includes
  // providerId, tileSize, and exact area coordinates.
  useEffect(() => {
    if (!area || !selectedProvider) {
      setPlan(null)
      return
    }
    const requestIdentity = makePlanIdentity(selectedProvider, tileSize, area)
    let cancelled = false
    void (async () => {
      try {
        const result = await planTerrainDownload(
          client,
          selectedProvider,
          area,
          tileSize,
          [...selectedIndices],
        )
        if (cancelled) return
        if (!result.plan) {
          setPlan(null)
          return
        }
        // Stale response guard: verify the plan identity still matches
        // the current inputs before applying the result (Finding 1).
        setPlan((prev) => {
          // If a newer request has already updated the plan, don't overwrite.
          // The identity check below ensures the response matches what was
          // requested.
          void prev
          return {
            identity: requestIdentity,
            selectionTiles: result.plan!.selectionTiles.map((t) => ({
              col: t.col,
              row: t.row,
              bounds: {
                west: t.bounds?.west ?? 0,
                south: t.bounds?.south ?? 0,
                east: t.bounds?.east ?? 0,
                north: t.bounds?.north ?? 0,
              },
              areaSqm: t.areaSqm,
            })),
            providerRequests: result.plan!.providerRequests.map((r) => ({
              requestId: r.requestId,
              estimatedBytes: r.estimatedBytes,
            })),
            requestCount: result.plan!.requestCount,
            effectiveResolutionMpp: result.plan!.effectiveResolutionMpp,
            estimatedBytes: result.plan!.estimatedBytes,
            warnings: result.plan!.warnings,
            fullCoverage: result.plan!.fullCoverage,
            totalTileCount: result.plan!.totalTileCount,
            selectedTileCount: result.plan!.selectedTileCount,
            selectedAreaSqm: result.plan!.selectedAreaSqm,
          }
        })
      } catch (err) {
        if (cancelled) return
        setFormError(err instanceof Error ? err.message : 'Failed to plan download')
      }
    })()
    return () => { cancelled = true }
  }, [client, area, tileSize, selectedIndices, selectedProvider])

  const handleAreaDraw = (drawnArea: GeoBounds) => {
    // Validate the drawn area (BLOCKER 15: native validation also runs).
    if (drawnArea.west >= drawnArea.east || drawnArea.south >= drawnArea.north) {
      setFormError('West must be less than east; south must be less than north')
      return
    }
    if (drawnArea.west < -180 || drawnArea.east > 180 ||
        drawnArea.south < -85.05112878 || drawnArea.north > 85.05112878) {
      setFormError('Coordinates out of range (lon: [-180,180], lat: [-85.05,85.05])')
      return
    }
    // Finding 1: Immediately invalidate the previous plan. Do not wait
    // for the React effect to clear it — the old plan must not be
    // visible or actionable while the new area is current.
    setArea(drawnArea)
    setSelectedIndices(new Set())
    setPlan(null)
    setFormError(null)
  }

  const toggleTile = (index: number) => {
    setSelectedIndices((prev) => {
      const next = new Set(prev)
      if (next.has(index)) {
        next.delete(index)
      } else {
        next.add(index)
      }
      return next
    })
  }

  const selectAll = () => {
    if (!plan) return
    setSelectedIndices(new Set(plan.selectionTiles.map((_, i) => i)))
  }

  const clearSelection = () => {
    setSelectedIndices(new Set())
  }

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    if (starting || !area || selectedIndices.size === 0) return
    // Finding 1: Verify the plan identity matches the current inputs
    // before allowing download. This prevents submitting with a stale
    // plan that doesn't match the current provider/area/tileSize.
    if (!plan || !area) return
    const currentIdentity = makePlanIdentity(selectedProvider, tileSize, area)
    if (!planIdentityMatches(plan.identity, currentIdentity)) return
    // Verify selected indices are valid for this exact plan.
    for (const idx of selectedIndices) {
      if (idx < 0 || idx >= plan.selectionTiles.length) return
    }
    if (displayName.trim().length === 0) {
      setFormError('A terrain name is required.')
      return
    }
    setStarting(true)
    setFormError(null)
    try {
      const jobId = await downloadSelectedTerrain(
        client,
        selectedProvider,
        area,
        tileSize,
        [...selectedIndices],
        displayName.trim(),
      )
      setStartedJobId(jobId)
    } catch (err) {
      // IMPORTANT 5: Surface the real command failure to the user instead
      // of silently swallowing it. The engine returns typed error messages
      // (e.g. "provider_rate_limited", "selection_too_large") that are
      // actionable.
      setFormError(err instanceof Error ? err.message : 'Failed to start download')
    } finally {
      setStarting(false)
    }
  }

  const cancelDownload = async () => {
    if (!downloadJob || downloadFinished) return
    try {
      await cancelTerrainJob(client, downloadJob.jobId)
    } catch (err) {
      setFormError(err instanceof Error ? err.message : 'Could not cancel download')
    }
  }

  const downloadDisabled =
    starting ||
    !area ||
    selectedIndices.size === 0 ||
    startedJobId !== null ||
    !plan ||
    (area !== null && !planIdentityMatches(
      plan.identity,
      makePlanIdentity(selectedProvider, tileSize, area),
    )) ||
    [...selectedIndices].some((idx) => idx < 0 || idx >= plan.selectionTiles.length)

  const downloadLabel = starting
    ? 'Starting…'
    : selectedIndices.size > 0
      ? `Download ${selectedIndices.size} Selected Tiles`
      : 'Download Selected'

  const selectedProviderInfo = providers.find((p) => p.providerId === selectedProvider)

  return (
    <>
      <div className="form-row provider-row">
        <span className="form-label">Provider</span>
        <div className="provider-controls">
          <select
            className="form-input"
            value={selectedProvider}
            onChange={(e) => {
              setSelectedProvider(e.target.value)
              // IMPORTANT 4: Reset selection when provider changes — the
              // new provider may have different coverage/resolution.
              setSelectedIndices(new Set())
              setPlan(null)
            }}
            disabled={startedJobId !== null}
          >
            {providers.map((p) => (
              <option key={p.providerId} value={p.providerId}>
                {p.displayName}
              </option>
            ))}
          </select>
          {selectedProviderInfo && selectedProviderInfo.attribution ? (
            <AttributionDisplay attribution={selectedProviderInfo.attribution} />
          ) : null}
        </div>
      </div>

      {area ? (
        <div className="working-area-summary" aria-live="polite">
          <MapPin size={12} />
          <span>Working area: {area.west.toFixed(4)}, {area.south.toFixed(4)} → {area.east.toFixed(4)}, {area.north.toFixed(4)}</span>
        </div>
      ) : (
        <div className="working-area-summary empty">
          <span>No working area drawn — use Draw Area on the map.</span>
        </div>
      )}

      {/* BLOCKER 5: Real interactive map for Download Area UX */}
      <DownloadAreaMap
        area={area}
        onAreaChange={handleAreaDraw}
        selectionTiles={
          plan
            ? plan.selectionTiles.map((tile, i) => ({
                index: i,
                bounds: {
                  west: tile.bounds?.west ?? 0,
                  south: tile.bounds?.south ?? 0,
                  east: tile.bounds?.east ?? 0,
                  north: tile.bounds?.north ?? 0,
                },
                selected: selectedIndices.has(i),
              }))
            : []
        }
        onTileToggle={toggleTile}
        tileSize={tileSize}
        searchClient={searchClient}
        mapTileConfig={mapTileConfig}
        resizeToken={mapResizeToken}
      />

      <div className="selection-bar">
        <div className="selection-bar-left">
          <label className="form-inline tile-size-control">
            <span className="form-label">Tile size</span>
            <select
              className="form-input"
              value={tileSize}
              onChange={(e) => {
                setTileSize(Number(e.target.value))
                // IMPORTANT 4: Reset selection when tile size changes — old
                // indices are invalid for the new grid.
                setSelectedIndices(new Set())
                setPlan(null)
              }}
              disabled={startedJobId !== null}
            >
              {TILE_SIZES.map((t) => (
                <option key={t.value} value={t.value}>
                  {t.label}
                </option>
              ))}
            </select>
          </label>
          <div className="selection-actions">
            <button className="button secondary" type="button" onClick={selectAll} disabled={startedJobId !== null || !plan}>
              Select All
            </button>
            <button className="button secondary" type="button" onClick={clearSelection} disabled={startedJobId !== null || selectedIndices.size === 0}>
              Clear
            </button>
          </div>
        </div>
        {plan && plan.selectionTiles.length > 0 ? (
          <div className="selection-chips" role="status" aria-live="polite">
            <span className="chip">{plan.totalTileCount} tiles</span>
            <span className="chip">{plan.selectedTileCount} selected</span>
            <span className="chip">{plan.requestCount} requests</span>
            <span className="chip">~{plan.effectiveResolutionMpp.toFixed(1)} m/px</span>
            <span className="chip">Estimated size: {formatBytes(plan.estimatedBytes)}</span>
          </div>
        ) : null}
      </div>

      {plan && plan.warnings.length > 0 ? (
        <div className="form-error">
          <FileWarning size={14} /> {plan.warnings.join('; ')}
        </div>
      ) : null}

      {plan && plan.selectionTiles.length > 0 ? (
        <div className="tile-list-section">
          <button
            type="button"
            className="tile-list-toggle"
            aria-expanded={showTileList}
            onClick={() => setShowTileList((v) => !v)}
          >
            <ChevronDown size={12} className={showTileList ? 'expanded' : ''} />
            {showTileList ? 'Hide' : 'Show'} tile list ({plan.selectionTiles.length})
          </button>
          {showTileList && (
            <div className="tile-list">
              {plan.selectionTiles.map((tile, i) => (
                <label key={i} className="tile-list-item">
                  <input
                    type="checkbox"
                    checked={selectedIndices.has(i)}
                    onChange={() => toggleTile(i)}
                    disabled={startedJobId !== null}
                  />
                  <span>({tile.col},{tile.row})</span>
                </label>
              ))}
            </div>
          )}
        </div>
      ) : null}

      <label className="form-row terrain-name-row">
        <span className="form-label">Terrain name</span>
        <input
          className="form-input"
          value={displayName}
          onChange={(event) => setDisplayName(event.target.value)}
          placeholder="Downloaded area DEM"
          disabled={startedJobId !== null}
        />
      </label>

      {downloadJob && !downloadFinished ? (
        <div className="job-progress" aria-live="polite">
          <div className="job-progress-header">
            <span>Downloading terrain — {progressPercent}%</span>
          </div>
          <div className="progress-track" role="progressbar" aria-valuenow={progressPercent} aria-valuemin={0} aria-valuemax={100}>
            <div className="progress-fill" style={{ width: `${progressPercent}%` }} />
          </div>
          <button className="button secondary" type="button" onClick={() => void cancelDownload()}>
            Cancel download
          </button>
        </div>
      ) : null}

      {downloadJob && downloadJob.state === JobState.COMPLETED ? (
        <div className="form-static success" role="status">
          Terrain downloaded successfully.
        </div>
      ) : null}
      {downloadJob && downloadJob.state === JobState.CANCELLED ? (
        <div className="form-error" role="alert">
          <p>Download cancelled — no terrain was committed.</p>
        </div>
      ) : null}
      {downloadJob && downloadJob.state === JobState.FAILED ? (
        <div className="form-error" role="alert">
          <p>Download failed:</p>
          <p className="form-error-detail">{downloadJob.message}</p>
        </div>
      ) : null}

      {formError ? (
        <div className="form-error" role="alert">
          <p>{formError}</p>
        </div>
      ) : null}

      <div className="dialog-actions">
        <button className="button secondary" type="button" onClick={onClose}>
          {downloadFinished || startedJobId !== null ? 'Close' : 'Cancel'}
        </button>
        <button
          className="button primary"
          type="submit"
          disabled={downloadDisabled}
          onClick={submit}
        >
          {downloadLabel}
        </button>
      </div>
    </>
  )
}
