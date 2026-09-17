import { useState } from 'react'
import { X } from 'lucide-react'
import type { EngineClient } from '../../lib/engineSession'
import { renameRoad } from './roadApi'
import { useSelectionStore } from '../../editor/selection/selectionStore'

interface RenameRoadDialogProps {
  client: EngineClient
  onClose: () => void
}

// Road rename dialog: the user enters a new name for the selected road.
export function RenameRoadDialog({ client, onClose }: RenameRoadDialogProps) {
  const [name, setName] = useState('')
  const [error, setError] = useState<string | null>(null)
  const [renaming, setRenaming] = useState(false)

  const selectedId = useSelectionStore((state) => state.primaryId)
  const roadId = selectedId && selectedId.startsWith('road:') ? selectedId.slice('road:'.length) : null

  const handleRename = async () => {
    if (!roadId) {
      setError('No road selected.')
      return
    }
    if (!name.trim()) {
      setError('Road name is required.')
      return
    }
    setRenaming(true)
    try {
      await renameRoad(client, roadId, name.trim())
      onClose()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to rename road.')
    } finally {
      setRenaming(false)
    }
  }

  return (
    <div className="dialog-overlay" role="dialog" aria-modal="true" aria-label="Rename Road">
      <div className="dialog">
        <div className="dialog-header">
          <h2>Rename Road</h2>
          <button className="dialog-close" type="button" onClick={onClose} aria-label="Close">
            <X size={16} />
          </button>
        </div>
        <div className="dialog-body">
          <label className="form-row" htmlFor="road-rename-name">
            <span className="form-label">New name</span>
            <input
              id="road-rename-name"
              className="form-input"
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="e.g. Highway 101"
              autoFocus
            />
          </label>
          {error ? (
            <div className="form-error" role="alert">
              <p>{error}</p>
            </div>
          ) : null}
        </div>
        <div className="dialog-actions">
          <button className="button" type="button" onClick={onClose} disabled={renaming}>
            Cancel
          </button>
          <button className="button primary" type="button" onClick={() => void handleRename()} disabled={renaming}>
            {renaming ? 'Renaming…' : 'Rename'}
          </button>
        </div>
      </div>
    </div>
  )
}
