import { type ReactNode, type RefObject } from 'react'
import { useLayoutStore } from './layoutStore'
import { ResizeHandle } from './ResizeHandle'

// Dockable/resizable editor layout. Supports left/right/bottom panel regions
// plus a central viewport/workspace and dockable ContextEditorHost.
// Panel visibility and resizing are driven by the layout store (persisted USER
// preferences, separate from project state — ADR-0009). The native viewport host
// is rendered in the center and continues to behave correctly with panel
// resize/hide/show (the host's ResizeObserver re-reports bounds on every layout change).
export interface EditorLayoutProps {
  leftPanel?: ReactNode
  rightPanel?: ReactNode
  bottomPanel?: ReactNode
  contextEditor?: ReactNode
  viewportHostRef: RefObject<HTMLDivElement | null>
  viewport: ReactNode
}

export function EditorLayout({
  leftPanel,
  rightPanel,
  bottomPanel,
  contextEditor,
  viewport,
}: EditorLayoutProps) {
  const left = useLayoutStore((state) => state.panels.left)
  const right = useLayoutStore((state) => state.panels.right)
  const bottom = useLayoutStore((state) => state.panels.bottom)
  const contextEditorState = useLayoutStore((state) => state.panels.contextEditor)

  return (
    <div className="editor-layout">
      <div className="editor-layout-row">
        {left.visible && leftPanel ? (
          <>
            <div className="editor-panel editor-panel-left" style={{ width: left.size }}>
              {leftPanel}
            </div>
            <ResizeHandle region="left" edge="right" ariaLabel="Resize outliner" />
          </>
        ) : null}
        <div className="editor-center">
          <div className="editor-viewport-slot">{viewport}</div>
          {contextEditor && contextEditorState?.visible ? (
            <>
              <ResizeHandle
                region="contextEditor"
                edge="top"
                ariaLabel="Resize context editor"
              />
              <div
                className="editor-panel editor-panel-context"
                style={{ height: contextEditorState.size }}
              >
                {contextEditor}
              </div>
            </>
          ) : null}
        </div>
        {right.visible && rightPanel ? (
          <>
            <ResizeHandle region="right" edge="left" ariaLabel="Resize inspector" />
            <div className="editor-panel editor-panel-right" style={{ width: right.size }}>
              {rightPanel}
            </div>
          </>
        ) : null}
      </div>
      {bottom.visible && bottomPanel ? (
        <>
          <ResizeHandle region="bottom" edge="top" ariaLabel="Resize bottom panel" />
          <div className="editor-panel editor-panel-bottom" style={{ height: bottom.size }}>
            {bottomPanel}
          </div>
        </>
      ) : null}
    </div>
  )
}
