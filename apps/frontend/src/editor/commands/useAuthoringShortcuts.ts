import { useEffect } from 'react'
import { useAuthoringDraftStore } from '../tools/authoringDraftStore'
import { useWorkspaceStore } from '../shell/workspaceStore'

export function useAuthoringShortcuts(commitPolyline: () => void) {
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      // Don't intercept when user is typing in inputs
      const target = e.target as HTMLElement | null
      if (
        target &&
        (target.tagName === 'INPUT' ||
          target.tagName === 'TEXTAREA' ||
          target.tagName === 'SELECT' ||
          target.isContentEditable)
      ) {
        return
      }

      // Only active in 'roads' workspace
      const activeWorkspace = useWorkspaceStore.getState().activeWorkspace
      if (activeWorkspace !== 'roads') return

      const { activeTool, setTool, clearDraft, removeLastDraftPoint, draftPoints } =
        useAuthoringDraftStore.getState()

      if (e.code === 'KeyV' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        e.preventDefault()
        clearDraft()
        setTool('select')
      } else if (e.code === 'KeyS' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        e.preventDefault()
        clearDraft()
        setTool('road.straight')
      } else if (e.code === 'KeyA' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        e.preventDefault()
        clearDraft()
        setTool('road.arc')
      } else if (e.code === 'KeyC' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        e.preventDefault()
        clearDraft()
        setTool('road.clothoid')
      } else if (e.code === 'KeyP' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        e.preventDefault()
        clearDraft()
        setTool('road.polyline')
      } else if (e.code === 'Escape') {
        e.preventDefault()
        clearDraft()
        setTool('select')
      } else if (e.code === 'Enter') {
        if (activeTool === 'road.polyline' && draftPoints.length >= 2) {
          e.preventDefault()
          commitPolyline()
        }
      } else if (e.code === 'Backspace' || e.code === 'Delete') {
        if (draftPoints.length > 0) {
          e.preventDefault()
          removeLastDraftPoint()
        }
      }
    }

    window.addEventListener('keydown', handleKeyDown)
    return () => window.removeEventListener('keydown', handleKeyDown)
  }, [commitPolyline])
}
