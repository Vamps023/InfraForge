import { useState } from 'react'
import { X } from 'lucide-react'
import type { EngineClient } from '../../lib/engineSession'
import { createRoad, listRoads } from './roadApi'
import { useRoadToolStore } from './roadToolStore'

interface CreateRoadDialogProps {
  client: EngineClient
  onClose: () => void
}

// Road creation dialog: the user enters a road name and a source polyline
// (as easting/northing pairs in canonical project coordinates). The
// engine runs the deterministic alignment fitter to produce a canonical
// ReferenceAlignment. The dialog never fabricates geometry.
export function CreateRoadDialog({ client, onClose }: CreateRoadDialogProps) {
  const [name, setName] = useState('')
  const [coordinates, setCoordinates] = useState('')
  const [tolerance, setTolerance] = useState('1.0')
  const [error, setError] = useState<string | null>(null)
  const [creating, setCreating] = useState(false)

  const handleDraw = () => {
    setError(null)
    const tol = Number.parseFloat(tolerance)
    if (!name.trim()) { setError('Road name is required.'); return }
    if (!Number.isFinite(tol) || tol <= 0) {
      setError('Position tolerance must be a positive number.'); return
    }
    useRoadToolStore.getState().begin(name.trim(), tol, null)
    onClose()
  }

  const handleCreate = async () => {
    setError(null)
    if (!name.trim()) {
      setError('Road name is required.')
      return
    }

    // Parse coordinates: one pair per line, easting northing separated by
    // whitespace or comma. At least 2 points are required for a polyline.
    const lines = coordinates.trim().split('\n').map((l) => l.trim()).filter((l) => l.length > 0)
    if (lines.length < 2) {
      setError('At least 2 coordinate pairs are required.')
      return
    }

    const eastings: number[] = []
    const northings: number[] = []
    for (let i = 0; i < lines.length; i++) {
      const parts = lines[i]!.split(/[\s,]+/)
      if (parts.length < 2) {
        setError(`Line ${i + 1}: expected easting and northing values.`)
        return
      }
      const e = Number.parseFloat(parts[0]!)
      const n = Number.parseFloat(parts[1]!)
      if (!Number.isFinite(e) || !Number.isFinite(n)) {
        setError(`Line ${i + 1}: invalid coordinate values.`)
        return
      }
      eastings.push(e)
      northings.push(n)
    }

    const tol = Number.parseFloat(tolerance)
    if (!Number.isFinite(tol) || tol <= 0) {
      setError('Position tolerance must be a positive number.')
      return
    }

    setCreating(true)
    try {
      await createRoad(client, name.trim(), eastings, northings, tol)
      await listRoads(client)
      onClose()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to create road.')
    } finally {
      setCreating(false)
    }
  }

  return (
    <div className="dialog-overlay" role="dialog" aria-label="Create Road">
      <div className="dialog-panel">
        <div className="dialog-header">
          <h2>Create Road</h2>
          <button className="dialog-close" type="button" onClick={onClose} aria-label="Close">
            <X size={16} />
          </button>
        </div>
        <div className="dialog-body">
          <div className="form-field">
            <label htmlFor="road-name">Road Name</label>
            <input
              id="road-name"
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="e.g. Highway 101"
              autoFocus
            />
          </div>
          <div className="form-field">
            <label htmlFor="road-coordinates">Source Polyline (Easting, Northing per line)</label>
            <textarea
              id="road-coordinates"
              value={coordinates}
              onChange={(e) => setCoordinates(e.target.value)}
              placeholder={'e.g.\n0 0\n100 0\n200 50'}
              rows={8}
              className="mono"
            />
          </div>
          <div className="form-field">
            <label htmlFor="road-tolerance">Position Tolerance (project units)</label>
            <input
              id="road-tolerance"
              type="text"
              value={tolerance}
              onChange={(e) => setTolerance(e.target.value)}
              placeholder="1.0"
            />
          </div>
          {error ? <div className="dialog-error">{error}</div> : null}
        </div>
        <div className="dialog-footer">
          <button className="button secondary" type="button" onClick={onClose} disabled={creating}>
            Cancel
          </button>
          <button className="button secondary" type="button" onClick={() => void handleCreate()} disabled={creating}>
            {creating ? 'Creating…' : 'Create from coordinates'}
          </button>
          <button className="button primary" type="button" onClick={handleDraw} disabled={creating}>
            Draw in viewport
          </button>
        </div>
      </div>
    </div>
  )
}
