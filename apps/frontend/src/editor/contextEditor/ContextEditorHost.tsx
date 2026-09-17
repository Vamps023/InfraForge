import { useState, useSyncExternalStore } from 'react'
import { ChevronDown, ChevronUp, X } from 'lucide-react'
import {
  contextEditorRegistry,
  type ContextEditorContext,
} from './contextEditorRegistry'
import { useSelectionStore } from '../selection/selectionStore'
import { useWorkspaceStore } from '../shell/workspaceStore'
import { useLayoutStore } from '../layout/layoutStore'

export function useActiveContextEditor() {
  const selectedIds = useSelectionStore((state) => state.selectedIds)
  const primaryId = useSelectionStore((state) => state.primaryId)
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)

  useSyncExternalStore(
    contextEditorRegistry.subscribe,
    () => contextEditorRegistry.getAll().length,
  )

  const context: ContextEditorContext = {
    activeWorkspace,
    selectedIds,
    primaryId,
  }

  const editor = contextEditorRegistry.resolve(context)
  return editor ? { editor, context } : null
}

// ContextEditorHost — dockable/resizable container for task-specific engineering
// editors (e.g. Road Profile, Cross-section, Topology) according to
// docs/06_UI_UX/WORKSPACE_MODEL.md and UX_SPEC.md.
// Sits below the main viewport and resizes it cleanly without floating overlays.
export function ContextEditorHost() {
  const isVisible = useLayoutStore((state) => state.panels.contextEditor.visible)
  const setVisible = useLayoutStore((state) => state.setPanelVisible)
  const active = useActiveContextEditor()

  // If no editor applies to this selection/workspace, dock is not rendered
  if (!active || !isVisible) {
    return null
  }

  return (
    <div className="context-editor-host" aria-label="Context editor">
      <div className="context-editor-header">
        <span className="context-editor-title">{active.editor.label}</span>
        <div className="context-editor-actions">
          <button
            type="button"
            className="icon-button"
            title="Close context editor"
            aria-label="Close context editor"
            onClick={() => setVisible('contextEditor', false)}
          >
            <X size={13} />
          </button>
        </div>
      </div>
      <div className="context-editor-content">
        {active.editor.render(active.context)}
      </div>
    </div>
  )
}
