import { useState } from 'react'
import { FolderOpen } from 'lucide-react'
import { TrafficSide } from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import { createProject } from './projectApi'
import { useProjectStore } from './projectStore'
import { CrsPicker } from './CrsPicker'

interface NewProjectDialogProps {
  client: EngineClient
  busy: boolean
  onClose: () => void
}

// The engine rejects latitude/longitude project CRS with this technical
// pattern; the dialog adds a plain-language explanation on top and keeps the
// engine detail visible for diagnostics. The engine remains the authority —
// nothing here intercepts or rewrites the value.
const GEOGRAPHIC_CRS_ERROR_PATTERN = /resolves to GEOGRAPHIC_CRS/

// New-project flow: the OS directory dialog stays in the desktop shell; the
// engine owns project creation, naming rules, and canonical metadata. The
// summary shown after success is the engine's authoritative result.
export function NewProjectDialog({ client, busy, onClose }: NewProjectDialogProps) {
  const [displayName, setDisplayName] = useState('')
  const [parentDirectory, setParentDirectory] = useState('')
  const [horizontalCrs, setHorizontalCrs] = useState('')
  const [linearUnit, setLinearUnit] = useState('metre')
  const [originEasting, setOriginEasting] = useState('0')
  const [originNorthing, setOriginNorthing] = useState('0')
  const [originHeight, setOriginHeight] = useState('0')
  const [verticalCrs, setVerticalCrs] = useState('')
  const [trafficSide, setTrafficSide] = useState<'LEFT' | 'RIGHT'>('RIGHT')
  const [formError, setFormError] = useState<string | null>(null)
  const [submitting, setSubmitting] = useState(false)

  const engineError = useProjectStore((state) => state.lastError?.message ?? null)
  const error = formError ?? engineError

  const pickDirectory = async () => {
    const desktop = window.infraforgeDesktop
    if (!desktop?.pickDirectory) {
      setFormError('The desktop shell did not expose a directory picker.')
      return
    }
    const selected = await desktop.pickDirectory({
      title: 'Choose the folder that will contain the project',
      buttonLabel: 'Select Folder',
    })
    if (selected) {
      setParentDirectory(selected)
      setFormError(null)
    }
  }

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    if (submitting || busy) {
      return
    }
    if (
      displayName.trim().length === 0 ||
      parentDirectory.trim().length === 0 ||
      horizontalCrs.trim().length === 0
    ) {
      setFormError('Project name, location, and horizontal CRS are required.')
      return
    }
    const easting = Number(originEasting)
    const northing = Number(originNorthing)
    const height = Number(originHeight)
    if (!Number.isFinite(easting) || !Number.isFinite(northing) || !Number.isFinite(height)) {
      setFormError('Origin coordinates must be finite numbers.')
      return
    }
    setSubmitting(true)
    setFormError(null)
    try {
      await createProject(client, {
        displayName: displayName.trim(),
        parentDirectory: parentDirectory.trim(),
        horizontalCrs: horizontalCrs.trim(),
        linearUnit,
        originEasting: easting,
        originNorthing: northing,
        originHeight: height,
        verticalCrs: verticalCrs.trim(),
        trafficSide: trafficSide === 'LEFT' ? TrafficSide.LEFT : TrafficSide.RIGHT,
      })
      onClose()
    } catch {
      // Engine failures are recorded in the projection store and shown via
      // engineError; closing the dialog is left to the user.
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="dialog-overlay" role="presentation">
      <form className="dialog" role="dialog" aria-modal="true" aria-label="New project" onSubmit={submit}>
        <h2>New project</h2>

        <label className="form-row">
          <span className="form-label">Project name</span>
          <input
            className="form-input"
            value={displayName}
            onChange={(event) => setDisplayName(event.target.value)}
            placeholder="Northgate Interchange"
            autoFocus
          />
        </label>

        <div className="form-row">
          <span className="form-label">Location</span>
          <div className="form-inline">
            <input
              className="form-input grow"
              value={parentDirectory}
              onChange={(event) => setParentDirectory(event.target.value)}
              placeholder="Parent folder for &lt;name&gt;.iforge"
            />
            <button
              className="button secondary"
              type="button"
              onClick={() => void pickDirectory()}
              disabled={!window.infraforgeDesktop?.pickDirectory}
            >
              <FolderOpen size={14} /> Browse…
            </button>
          </div>
        </div>

        <div className="form-row-pair">
          <div className="form-row">
            <span className="form-label">Horizontal CRS</span>
            <CrsPicker value={horizontalCrs} onChange={setHorizontalCrs} />
          </div>
          <label className="form-row">
            <span className="form-label">Linear unit</span>
            <select className="form-input" value={linearUnit} onChange={(event) => setLinearUnit(event.target.value)}>
              <option value="metre">metre</option>
              <option value="us_survey_foot">US survey foot</option>
            </select>
          </label>
        </div>

        <div className="form-row-pair">
          <label className="form-row">
            <span className="form-label">Origin easting</span>
            <input
              className="form-input"
              value={originEasting}
              onChange={(event) => setOriginEasting(event.target.value)}
            />
          </label>
          <label className="form-row">
            <span className="form-label">Origin northing</span>
            <input
              className="form-input"
              value={originNorthing}
              onChange={(event) => setOriginNorthing(event.target.value)}
            />
          </label>
        </div>

        <div className="form-row-pair">
          <label className="form-row">
            <span className="form-label">Origin height</span>
            <input
              className="form-input"
              value={originHeight}
              onChange={(event) => setOriginHeight(event.target.value)}
            />
          </label>
          <label className="form-row">
            <span className="form-label">Vertical CRS (optional)</span>
            <input
              className="form-input"
              value={verticalCrs}
              onChange={(event) => setVerticalCrs(event.target.value)}
              placeholder="EPSG:3855"
            />
          </label>
        </div>

        <div className="form-row-pair">
          <label className="form-row">
            <span className="form-label">Traffic side</span>
            <select
              className="form-input"
              value={trafficSide}
              onChange={(event) => setTrafficSide(event.target.value as 'LEFT' | 'RIGHT')}
            >
              <option value="RIGHT">Right</option>
              <option value="LEFT">Left</option>
            </select>
          </label>
          <div className="form-row">
            <span className="form-label">Axis convention</span>
            <div className="form-static">Easting / Northing / Up (metre-based)</div>
          </div>
        </div>

        {error ? (
          <div className="form-error" role="alert">
            {error && GEOGRAPHIC_CRS_ERROR_PATTERN.test(error) ? (
              <>
                <p>
                  Project CRS must use linear coordinates such as metres or feet.{' '}
                  {horizontalCrs.trim() ? `${horizontalCrs.trim()} uses` : 'The entered CRS uses'} latitude/longitude
                  degrees.
                </p>
                <p className="form-error-detail">{error}</p>
              </>
            ) : (
              <p>{error}</p>
            )}
          </div>
        ) : null}

        <div className="dialog-actions">
          <button className="button secondary" type="button" onClick={onClose} disabled={submitting}>
            Cancel
          </button>
          <button className="button primary" type="submit" disabled={submitting || busy}>
            {submitting ? 'Creating…' : 'Create Project'}
          </button>
        </div>
      </form>
    </div>
  )
}
