import type { CSSProperties } from 'react'

// StatusDot — colored indicator dot for status bars and HUD elements.
// Uses semantic status tokens. Size is configurable via the `size` prop.
export type StatusDotTone = 'success' | 'warning' | 'danger' | 'muted' | 'info'

export interface StatusDotProps {
  tone: StatusDotTone
  size?: number
  className?: string
  'aria-label'?: string
}

export function StatusDot({ tone, size = 7, className, ...ariaProps }: StatusDotProps) {
  const style: CSSProperties = { width: size, height: size }
  const cls = `status-dot status-dot--${tone}${className ? ` ${className}` : ''}`
  return <span className={cls} style={style} {...ariaProps} />
}
