import type { LucideIcon } from 'lucide-react'
import {
  Download,
  FolderOpen,
  MapPin,
  Plus,
  Trash2,
  Undo2,
  Redo2,
  RefreshCw,
  Check,
  X,
  Upload,
  MousePointer2,
  Slash,
  Circle,
  TrendingUp,
  PenLine,
} from 'lucide-react'
import { useWorkspaceStore } from './workspaceStore'
import {
  workspaceRegistry,
  type WorkspaceToolGroup,
  type WorkspaceToolItem,
} from '../workspaces/workspaceRegistry'
import { Tooltip } from '../../ui/Tooltip'
import {
  useCommandExecutor,
  type CommandContext,
} from '../commands/useCommands'
import { useRoadToolStore } from '../../features/road/roadToolStore'

// Default icon map for standard commands when tool definition doesn't override
const ICON_MAP: Record<string, LucideIcon> = {
  'terrain.import-local': FolderOpen,
  'terrain.download-area': Download,
  'terrain.export': Upload,
  'project.georeference': MapPin,
  'road.create': Plus,
  'road.delete': Trash2,
  'road.fit-source': RefreshCw,
  'road.undo': Undo2,
  'road.redo': Redo2,
  'road.finish-drawing': Check,
  'road.cancel-drawing': X,
  'road.tool.select': MousePointer2,
  'road.tool.straight': Slash,
  'road.tool.arc': Circle,
  'road.tool.clothoid': TrendingUp,
  'road.tool.polyline': PenLine,
}

export interface ContextToolShelfProps {
  context: CommandContext
}

// ContextToolShelf — registry-driven contextual tool shelf.
// Replaces hardcoded ContextToolbar switches with configuration from WorkspaceDefinition.
// All buttons route through the central command registry (executeCommand) so
// safety/availability gating is 100% unified with menus, palette, and shortcuts.
export function ContextToolShelf({ context }: ContextToolShelfProps) {
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)
  const def = workspaceRegistry.get(activeWorkspace)

  if (!def || !def.availability.enabled || def.toolGroups.length === 0) {
    return null
  }

  return (
    <div
      className="context-toolbar context-tool-shelf"
      aria-label={`${def.label} workspace actions`}
    >
      <span className="context-toolbar-label">{def.label}</span>
      {def.toolGroups.map((group) => (
        <ToolGroupRenderer
          key={group.id}
          group={group}
          context={context}
        />
      ))}
    </div>
  )
}

function RoadDrawingToolGroup({
  group,
  context,
  pointCount,
}: {
  group: WorkspaceToolGroup
  context: CommandContext
  pointCount: number
}) {
  const finishDrawing = useCommandExecutor('road.finish-drawing', context)
  const cancelDrawing = useCommandExecutor('road.cancel-drawing', context)

  return (
    <>
      <div className="context-toolbar-divider" />
      <div
        className={`context-toolbar-group ${group.className ?? ''}`.trim()}
        role="group"
        aria-label={group.label}
      >
        <span className="context-toolbar-hint">{pointCount} control points</span>
        <button
          type="button"
          className="tool-button active"
          disabled={!finishDrawing.availability.enabled}
          onClick={() => void finishDrawing.run()}
        >
          <Check size={14} /> Finish
        </button>
        <button
          type="button"
          className="tool-button"
          disabled={!cancelDrawing.availability.enabled}
          onClick={() => void cancelDrawing.run()}
        >
          <X size={14} /> Cancel
        </button>
      </div>
    </>
  )
}

function ToolGroupRenderer({
  group,
  context,
}: {
  group: WorkspaceToolGroup
  context: CommandContext
}) {
  const drawing = useRoadToolStore((state) => state.mode === 'drawing')
  const pointCount = useRoadToolStore((state) => state.points.length)

  // Handle special dynamic drawing group
  if (group.id === 'road-drawing') {
    if (!drawing) {
      return null
    }
    return (
      <RoadDrawingToolGroup
        group={group}
        context={context}
        pointCount={pointCount}
      />
    )
  }

  // Evaluate custom condition if provided
  if (group.condition && !group.condition()) {
    return null
  }

  return (
    <>
      <div className="context-toolbar-divider" />
      <div
        className={`context-toolbar-group ${group.className ?? ''}`.trim()}
        role="group"
        aria-label={group.label}
      >
        {group.tools.map((tool) => (
          <ToolButtonRenderer
            key={tool.commandId}
            tool={tool}
            context={context}
          />
        ))}
      </div>
    </>
  )
}

function ToolButtonRenderer({
  tool,
  context,
}: {
  tool: WorkspaceToolItem
  context: CommandContext
}) {
  const executor = useCommandExecutor(tool.commandId, context)
  const IconComponent = tool.icon ?? ICON_MAP[tool.commandId]
  const label = tool.label ?? executor.command?.label ?? tool.commandId
  const tooltip =
    executor.availability.disabledReason ?? tool.tooltip ?? executor.command?.description ?? label

  return (
    <Tooltip label={tooltip}>
      <button
        type="button"
        className={`tool-button${tool.primary ? ' active' : ''}`}
        disabled={!executor.availability.enabled}
        onClick={() => void executor.run()}
      >
        {IconComponent ? <IconComponent size={14} /> : null}
        {label}
      </button>
    </Tooltip>
  )
}
