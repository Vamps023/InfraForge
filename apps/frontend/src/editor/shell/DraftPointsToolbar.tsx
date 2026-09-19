import { Check, Undo2, X } from 'lucide-react'
import { useAuthoringDraftStore } from '../tools/authoringDraftStore'
import { AUTHORING_TOOLS } from '../tools/authoringToolTypes'

export interface DraftPointsToolbarProps {
  onCommit: () => void
  onCancel: () => void
}

export function DraftPointsToolbar({ onCommit, onCancel }: DraftPointsToolbarProps) {
  const activeTool = useAuthoringDraftStore((state) => state.activeTool)
  const draftPoints = useAuthoringDraftStore((state) => state.draftPoints)
  const removeLastDraftPoint = useAuthoringDraftStore((state) => state.removeLastDraftPoint)
  const metrics = useAuthoringDraftStore((state) => state.metrics)

  if (activeTool === 'select' || draftPoints.length === 0) {
    return null
  }

  const def = AUTHORING_TOOLS[activeTool]
  const count = draftPoints.length
  const minPoints = def.minPoints || 2
  const canComplete = count >= minPoints
  const length = metrics.totalLength

  const formattedLength =
    length >= 1000
      ? `${(length / 1000).toFixed(2)} km`
      : `${length.toFixed(1)} m`

  return (
    <div
      className="draft-points-toolbar"
      role="toolbar"
      aria-label="Active road construction draft"
    >
      <div className="draft-points-badge-group">
        <span className="draft-vertex-count">
          {count} {count === 1 ? 'vertex' : 'vertices'}
        </span>
        {length > 0 && (
          <span className="draft-length-display">{formattedLength}</span>
        )}
      </div>

      <span className="draft-hint-text">
        {def.statusHint || 'Click to place point · Enter to complete · Esc to cancel'}
      </span>

      <div className="draft-actions-group">
        {count > 1 && (
          <button
            type="button"
            className="draft-btn outline"
            onClick={removeLastDraftPoint}
            title="Remove last point (Backspace)"
          >
            <Undo2 size={13} />
            <span>Step Back</span>
            <kbd className="kbd-shortcut">⌫</kbd>
          </button>
        )}

        <button
          type="button"
          className="draft-btn primary"
          disabled={!canComplete}
          onClick={onCommit}
          title="Finish and create road (Enter)"
        >
          <Check size={13} />
          <span>Complete</span>
          <kbd className="kbd-shortcut">↵</kbd>
        </button>

        <button
          type="button"
          className="draft-btn ghost"
          onClick={onCancel}
          title="Discard draft (Esc)"
        >
          <X size={13} />
          <span>Cancel</span>
          <kbd className="kbd-shortcut">Esc</kbd>
        </button>
      </div>
    </div>
  )
}
