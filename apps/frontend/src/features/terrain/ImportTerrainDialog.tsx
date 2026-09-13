import { useEffect, useRef, useState } from 'react'
import { Download, FileWarning, FolderOpen, MapPin, Square } from 'lucide-react'
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
import { DownloadAreaMap, type GeoBounds, type SelectionTileInfo } from './DownloadAreaMap'

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
  const [sourceMode, setSourceMode] = useState<SourceMode>('local-file')

  return (
    <div className="dialog-overlay" role="presentation">
      <div className="dialog" role="dialog" aria-modal="true" aria-label="Import terrain">
        <h2>Import terrain</h2>

        <div className="form-row">
          <span className="form-label">Source</span>
          <div className="form-inline">
            <button
              className={`button ${sourceMode === 'local-file' ? 'primary' : 'secondary'}`}
              type="button"
              onClick={() => setSourceMode('local-file')}
            >
              <FolderOpen size={14} /> Local File
            </button>
            <button
              className={`button ${sourceMode === 'download-area' ? 'primary' : 'secondary'}`}
              type="button"
              onClick={() => setSourceMode('download-area')}
            >
              <Download size={14} /> Download Area
            </button>
          </div>
        </div>

        {sourceMode === 'local-file' ? (
          <LocalFileImport client={client} onClose={onClose} />
        ) : (
          <DownloadAreaImport client={client} onClose={onClose} />
        )}
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
    if (starting || !probe) {
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
    setStarting(true)
    setFormError(null)
    try {
      const jobId = await importTerrainDataset(client, path.trim(), displayName.trim())
      setStartedJobId(jobId)
    } catch {
      // Engine failures are recorded in the terrain store and shown below.
    } finally {
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
        <span className="form-label">DEM source</span>
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
        <div className="form-static">
          <div>
            <strong>CRS:</strong> {probe.crsName || probe.source.crsDefinition}
            {probe.crsAuthority && probe.crsCode ? ` (${probe.crsAuthority}:${probe.crsCode})` : ''}
          </div>
          <div>
            {probe.source.width} × {probe.source.height} pixels ·{' '}
            {Number(probe.source.pixelSizeX).toPrecision(6)} ×{' '}
            {Number(probe.source.pixelSizeY).toPrecision(6)} {probe.source.elevationUnit === 'metre' ? 'm' : 'units'}{' '}
            per pixel · {probe.source.elevationUnit} elevation
          </div>
          <div>
            {probe.source.sampleType} samples · {probe.source.hasNodata ? 'NoData present' : 'no NoData'} ·{' '}
            {formatBytes(probe.source.fileBytes)} · {probe.source.format}
          </div>
        </div>
      ) : null}

      <label className="form-row">
        <span className="form-label">Terrain name</span>
        <input
          className="form-input"
          value={displayName}
          onChange={(event) => setDisplayName(event.target.value)}
          placeholder="Area DEM"
          disabled={startedJobId !== null}
        />
      </label>

      {importJob && !importFinished ? (
        <div className="form-static" aria-live="polite">
          <div>
            Importing — {importJob.label || 'working'} ({progressPercent}%)
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
        <div className="form-static" role="status">
          Import complete. The terrain dataset is registered and its render tiles are generating or present; see the
          Operations tab for tile-generation progress.
        </div>
      ) : null}
      {importJob && importJob.state === JobState.CANCELLED ? (
        <div className="form-error" role="alert">
          <p>Import cancelled. No terrain data was committed to the project.</p>
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
        <button className="button primary" type="submit" disabled={starting || !probe || startedJobId !== null} onClick={submit}>
          {starting ? 'Starting…' : 'Import'}
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

function DownloadAreaImport({ client, onClose }: { client: EngineClient; onClose: () => void }) {
  const [area, setArea] = useState<DrawnArea | null>(null)
  const [tileSize, setTileSize] = useState(4000)
  const [selectedIndices, setSelectedIndices] = useState<Set<number>>(new Set())
  const [providers, setProviders] = useState<{ providerId: string; displayName: string }[]>([])
  const [selectedProvider, setSelectedProvider] = useState('')
  const [plan, setPlan] = useState<{
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
        setProviders(result.providers.map((p) => ({ providerId: p.providerId, displayName: p.displayName })))
        if (result.providers.length > 0 && !selectedProvider) {
          setSelectedProvider(result.providers[0]!.providerId)
        }
      } catch {
        // Providers will be empty; user can still draw but not download.
      }
    })()
  }, [client])

  // Fetch plan when area, tile size, or selection changes.
  useEffect(() => {
    if (!area || !selectedProvider) {
      setPlan(null)
      return
    }
    void (async () => {
      try {
        const result = await planTerrainDownload(
          client,
          selectedProvider,
          area,
          tileSize,
          [...selectedIndices],
        )
        if (!result.plan) {
          setPlan(null)
          return
        }
        setPlan({
          selectionTiles: result.plan.selectionTiles.map((t) => ({
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
          providerRequests: result.plan.providerRequests.map((r) => ({
            requestId: r.requestId,
            estimatedBytes: r.estimatedBytes,
          })),
          requestCount: result.plan.requestCount,
          effectiveResolutionMpp: result.plan.effectiveResolutionMpp,
          estimatedBytes: result.plan.estimatedBytes,
          warnings: result.plan.warnings,
          fullCoverage: result.plan.fullCoverage,
          totalTileCount: result.plan.totalTileCount,
          selectedTileCount: result.plan.selectedTileCount,
          selectedAreaSqm: result.plan.selectedAreaSqm,
        })
      } catch (err) {
        setFormError(err instanceof Error ? err.message : 'Failed to plan download')
      }
    })()
  }, [client, area, tileSize, selectedIndices, selectedProvider])

  const handleAreaDraw = (drawnArea: GeoBounds) => {
    // Validate the drawn area (BLOCKER 15: native validation also runs).
    if (drawnArea.west >= drawnArea.east || drawnArea.south >= drawnArea.north) {
      setFormError('West must be less than east; south must be less than north')
      return
    }
    if (drawnArea.west < -180 || drawnArea.east > 180 ||
        drawnArea.south < -90 || drawnArea.north > 90) {
      setFormError('Coordinates out of range (lon: [-180,180], lat: [-90,90])')
      return
    }
    setArea(drawnArea)
    setSelectedIndices(new Set())
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
    } catch {
      // Engine failures are recorded in the terrain store.
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

  return (
    <>
      <div className="form-row">
        <span className="form-label">Provider</span>
        <select
          className="form-input"
          value={selectedProvider}
          onChange={(e) => setSelectedProvider(e.target.value)}
          disabled={startedJobId !== null}
        >
          {providers.map((p) => (
            <option key={p.providerId} value={p.providerId}>
              {p.displayName}
            </option>
          ))}
        </select>
      </div>

      <div className="form-row">
        <span className="form-label">Working area</span>
        <div className="form-inline">
          {area ? (
            <span className="form-static">
              <MapPin size={14} /> {area.west.toFixed(4)}, {area.south.toFixed(4)} → {area.east.toFixed(4)}, {area.north.toFixed(4)}
            </span>
          ) : (
            <span className="form-static">No area drawn — use the map below</span>
          )}
        </div>
      </div>

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
      />

      <div className="form-row">
        <span className="form-label">Tile size</span>
        <select
          className="form-input"
          value={tileSize}
          onChange={(e) => setTileSize(Number(e.target.value))}
          disabled={startedJobId !== null}
        >
          {TILE_SIZES.map((t) => (
            <option key={t.value} value={t.value}>
              {t.label}
            </option>
          ))}
        </select>
      </div>

      {plan && plan.selectionTiles.length > 0 ? (
        <div className="form-static">
          <div>
            <strong>Tiles:</strong> {plan.totalTileCount} total · {plan.selectedTileCount} selected
          </div>
          <div>
            <strong>Selected area:</strong> {(plan.selectedAreaSqm / 1_000_000).toFixed(3)} km²
          </div>
          <div>
            <strong>Provider requests:</strong> {plan.requestCount} (deduplicated)
          </div>
          <div>
            <strong>Effective resolution:</strong> {plan.effectiveResolutionMpp.toFixed(1)} m/px
          </div>
          <div>
            <strong>Estimated size:</strong> {formatBytes(plan.estimatedBytes)}
          </div>
          {plan.warnings.length > 0 ? (
            <div className="form-error">
              <FileWarning size={14} /> {plan.warnings.join('; ')}
            </div>
          ) : null}
          <div className="form-inline" style={{ marginTop: '0.5rem' }}>
            <button className="button secondary" type="button" onClick={selectAll} disabled={startedJobId !== null}>
              Select All
            </button>
            <button className="button secondary" type="button" onClick={clearSelection} disabled={startedJobId !== null}>
              Clear
            </button>
          </div>
          <div style={{ marginTop: '0.5rem', maxHeight: '200px', overflowY: 'auto' }}>
            {plan.selectionTiles.map((tile, i) => (
              <label key={i} className="form-inline" style={{ display: 'inline-flex', margin: '2px' }}>
                <input
                  type="checkbox"
                  checked={selectedIndices.has(i)}
                  onChange={() => toggleTile(i)}
                  disabled={startedJobId !== null}
                />
                <span style={{ fontSize: '0.85em' }}>
                  ({tile.col},{tile.row})
                </span>
              </label>
            ))}
          </div>
        </div>
      ) : null}

      <label className="form-row">
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
        <div className="form-static" aria-live="polite">
          <div>
            Downloading — {downloadJob.label || 'working'} ({progressPercent}%)
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
        <div className="form-static" role="status">
          Download complete. The terrain dataset is registered and its render tiles are generating.
        </div>
      ) : null}
      {downloadJob && downloadJob.state === JobState.CANCELLED ? (
        <div className="form-error" role="alert">
          <p>Download cancelled. No terrain data was committed to the project.</p>
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
          disabled={starting || !area || selectedIndices.size === 0 || startedJobId !== null}
          onClick={submit}
        >
          {starting ? 'Starting…' : 'Download Selected'}
        </button>
      </div>
    </>
  )
}
