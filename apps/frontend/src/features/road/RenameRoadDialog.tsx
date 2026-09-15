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
    <div className="dialog-overlay" role="dialog" aria-label="Rename Road">
      <div className="dialog-panel">
        <div className="dialog-header">
          <h2>Rename Road</h2>
          <button className="dialog-close" type="button" onClick={onClose} aria-label="Close">
            <X size={16} />
          </button>
        </div>
        <div className="dialog-body">
          <div className="form-field">
            <label htmlFor="road-rename-name">New Name</label>
            <input
              id="road-rename-name"
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="e.g. Highway 101"
              autoFocus
            />
          </div>
          {error ? <div className="dialog-error">{error}</div> : null}
        </div>
        <div className="dialog-footer">
          <button className="button secondary" type="button" onClick={onClose} disabled={renaming}>
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
