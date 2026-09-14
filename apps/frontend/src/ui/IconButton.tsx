import { type ReactNode, type ButtonHTMLAttributes } from 'react'
import { Tooltip } from './Tooltip'

// IconButton — icon-only button with tooltip, accessible label, and
// full interaction state support (default/hover/active/selected/disabled).
// Uses aria-label for screen readers; the tooltip is visual-only.
export interface IconButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  'aria-label': string
  tooltip?: string
  active?: boolean
  children: ReactNode
}

export function IconButton({
  'aria-label': ariaLabel,
  tooltip,
  active = false,
  disabled = false,
  children,
  className = '',
  ...rest
}: IconButtonProps) {
  const cls = `if-icon-button${active ? ' if-icon-button--active' : ''}${disabled ? ' if-icon-button--disabled' : ''}${className ? ` ${className}` : ''}`
  const button = (
    <button
      type="button"
      className={cls}
      aria-label={ariaLabel}
      aria-pressed={active}
      disabled={disabled}
      {...rest}
    >
      {children}
    </button>
  )
  // Tooltip is visual-only; the aria-label already covers screen readers.
  // When disabled, skip the tooltip wrapper so hover events don't fire.
  if (disabled || !tooltip) {
    return button
  }
  return <Tooltip label={tooltip}>{button}</Tooltip>
}
