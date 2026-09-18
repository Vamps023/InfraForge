import { Tooltip } from '../../ui/Tooltip'
import { useAuthoringDraftStore } from '../tools/authoringDraftStore'
import {
  AUTHORING_TOOLS,
  type AuthoringToolId,
} from '../tools/authoringToolTypes'

const PRIMARY_TOOLS: AuthoringToolId[] = [
  'select',
  'road.straight',
  'road.arc',
  'road.clothoid',
  'road.polyline',
]

export function ToolRail() {
  const activeTool = useAuthoringDraftStore((state) => state.activeTool)
  const setTool = useAuthoringDraftStore((state) => state.setTool)
  const draftPoints = useAuthoringDraftStore((state) => state.draftPoints)
  const clearDraft = useAuthoringDraftStore((state) => state.clearDraft)

  const handleSelectTool = (id: AuthoringToolId) => {
    if (activeTool !== id) {
      clearDraft()
      setTool(id)
    }
  }

  return (
    <aside className="authoring-tool-rail" aria-label="Authoring tools">
      {PRIMARY_TOOLS.map((toolId) => {
        const def = AUTHORING_TOOLS[toolId]
        if (!def) return null
        const Icon = def.icon
        const isActive = activeTool === toolId
        const pointCount = isActive ? draftPoints.length : 0

        return (
          <Tooltip
            key={toolId}
            label={`${def.label} (${def.shortcut}) — ${def.description}`}
            side="right"
          >
            <button
              type="button"
              className={`authoring-rail-button${isActive ? ' active' : ''}`}
              aria-label={def.label}
              aria-pressed={isActive}
              onClick={() => handleSelectTool(toolId)}
            >
              <Icon size={18} />
              <span className="authoring-rail-badge">{def.shortcut}</span>
              {pointCount > 0 && (
                <span className="authoring-rail-point-count">{pointCount}</span>
              )}
            </button>
          </Tooltip>
        )
      })}
    </aside>
  )
}
