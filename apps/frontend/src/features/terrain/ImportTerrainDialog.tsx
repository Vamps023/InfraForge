import { useRef, useState } from 'react'
import { FileWarning, FolderOpen } from 'lucide-react'
import { JobState } from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import { cancelTerrainJob, importTerrainDataset, probeTerrainSource } from './terrainApi'
import { useTerrainStore } from './terrainStore'

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

// Terrain import flow: the OS file dialog stays in the desktop shell, the
// engine probes the source (CRS validation is explicit and authoritative),
// and import runs as a cancellable native job with real progress. The
// dialog never fabricates CRS data or progress.
export function ImportTerrainDialog({ client, onClose }: ImportTerrainDialogProps) {
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

  // The import job record once the job has been started (progress, cancel).
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
        ? Math.min(100, Math.floor((Number(importJob.processed ?? 0n) * 100) / Number(importJob.total)))
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
    <div className="dialog-overlay" role="presentation">
      <form className="dialog" role="dialog" aria-modal="true" aria-label="Import terrain" onSubmit={submit}>
        <h2>Import terrain</h2>

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
          <button className="button primary" type="submit" disabled={starting || !probe || startedJobId !== null}>
            {starting ? 'Starting…' : 'Import'}
          </button>
        </div>
      </form>
    </div>
  )
}
