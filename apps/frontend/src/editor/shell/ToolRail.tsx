import {
  MousePointer,
  Minus,
  Spline,
  GitCommit,
  Share2,
  Move,
  PlusCircle,
  Trash2,
} from 'lucide-react'
import { Tooltip } from '../../ui/Tooltip'
import { useAuthoringDraftStore } from '../tools/authoringDraftStore'
import { useRoadToolStore } from '../../features/road/roadToolStore'
import { useRoadStore } from '../../features/road/roadStore'
import { deleteRoad } from '../../features/road/roadApi'
import type { EngineClient } from '../../lib/engineSession'

export interface ToolRailProps {
  client?: EngineClient | null
}

export function ToolRail({ client }: ToolRailProps) {
  const activeTool = useAuthoringDraftStore((state) => state.activeTool)
  const setTool = useAuthoringDraftStore((state) => state.setTool)
  const draftPoints = useAuthoringDraftStore((state) => state.draftPoints)
  const clearDraft = useAuthoringDraftStore((state) => state.clearDraft)

  const roadToolMode = useRoadToolStore((state) => state.mode)
  const selectedRoadId = useRoadStore((state) => state.selectedRoadId)

  const handleSelectAuthoring = (tool: typeof activeTool) => {
    if (activeTool !== tool) {
      clearDraft()
      useRoadToolStore.getState().cancel()
      setTool(tool)
    }
  }

  const handleDeleteSelected = async () => {
    if (selectedRoadId && client) {
      await deleteRoad(client, selectedRoadId).catch(() => undefined)
    }
  }

  return (
    <nav className="ogs-tool-rail" aria-label="Authoring tool rail">
      {/* Group: SELECT */}
      <div className="tool-rail-group">
        <span className="tool-rail-group-label">SELECT</span>
        <Tooltip label="Select Tool (V) — select roads and junctions in viewport" side="right">
          <button
            type="button"
            className={`rail-tool-btn${activeTool === 'select' && roadToolMode === 'idle' ? ' active' : ''}`}
            onClick={() => handleSelectAuthoring('select')}
            aria-label="Select tool"
            aria-pressed={activeTool === 'select'}
          >
            <MousePointer size={18} />
            <span className="rail-shortcut-badge">V</span>
          </button>
        </Tooltip>
      </div>

      <div className="tool-rail-divider" />

      {/* Group: INSERT */}
      <div className="tool-rail-group">
        <span className="tool-rail-group-label">INSERT</span>

        <Tooltip label="Straight Segment (S) — 2 points" side="right">
          <button
            type="button"
            className={`rail-tool-btn${activeTool === 'road.straight' ? ' active' : ''}`}
            onClick={() => handleSelectAuthoring('road.straight')}
            aria-label="Straight road tool"
            aria-pressed={activeTool === 'road.straight'}
          >
            <Minus size={18} />
            <span className="rail-shortcut-badge">S</span>
            {activeTool === 'road.straight' && draftPoints.length > 0 && (
              <span className="rail-count-badge">{draftPoints.length}</span>
            )}
          </button>
        </Tooltip>

        <Tooltip label="Circular Arc (A) — 3-point circular curve" side="right">
          <button
            type="button"
            className={`rail-tool-btn${activeTool === 'road.arc' ? ' active' : ''}`}
            onClick={() => handleSelectAuthoring('road.arc')}
            aria-label="Arc road tool"
            aria-pressed={activeTool === 'road.arc'}
          >
            <GitCommit size={18} />
            <span className="rail-shortcut-badge">A</span>
            {activeTool === 'road.arc' && draftPoints.length > 0 && (
              <span className="rail-count-badge">{draftPoints.length}</span>
            )}
          </button>
        </Tooltip>

        <Tooltip label="Clothoid Transition (C) — spiral Euler curve" side="right">
          <button
            type="button"
            className={`rail-tool-btn${activeTool === 'road.clothoid' ? ' active' : ''}`}
            onClick={() => handleSelectAuthoring('road.clothoid')}
            aria-label="Clothoid road tool"
            aria-pressed={activeTool === 'road.clothoid'}
          >
            <Spline size={18} />
            <span className="rail-shortcut-badge">C</span>
            {activeTool === 'road.clothoid' && draftPoints.length > 0 && (
              <span className="rail-count-badge">{draftPoints.length}</span>
            )}
          </button>
        </Tooltip>

        <Tooltip label="Multi-Segment Polyline (P) — continuous alignment" side="right">
          <button
            type="button"
            className={`rail-tool-btn${activeTool === 'road.polyline' ? ' active' : ''}`}
            onClick={() => handleSelectAuthoring('road.polyline')}
            aria-label="Polyline road tool"
            aria-pressed={activeTool === 'road.polyline'}
          >
            <Share2 size={18} />
            <span className="rail-shortcut-badge">P</span>
            {activeTool === 'road.polyline' && draftPoints.length > 0 && (
              <span className="rail-count-badge">{draftPoints.length}</span>
            )}
          </button>
        </Tooltip>
      </div>

      <div className="tool-rail-divider" />

      {/* Group: MODIFY */}
      <div className="tool-rail-group">
        <span className="tool-rail-group-label">MODIFY</span>

        <Tooltip label="Delete Selected Road (Del)" side="right">
          <button
            type="button"
            className="rail-tool-btn danger-hover"
            disabled={!selectedRoadId}
            onClick={() => void handleDeleteSelected()}
            aria-label="Delete selected road"
          >
            <Trash2 size={18} />
            <span className="rail-shortcut-badge">Del</span>
          </button>
        </Tooltip>
      </div>
    </nav>
  )
}
