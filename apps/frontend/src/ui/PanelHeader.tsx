import { type ReactNode } from 'react'

// PanelHeader — title bar for dockable panels. Supports optional
// trailing actions (e.g. panel-specific buttons).
export interface PanelHeaderProps {
  title: string
  actions?: ReactNode
}

export function PanelHeader({ title, actions }: PanelHeaderProps) {
  return (
    <div className="panel-title-row">
      <span>{title}</span>
      {actions ? <div className="panel-header-actions">{actions}</div> : null}
    </div>
  )
}
