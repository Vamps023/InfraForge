import { type ReactElement, type ReactNode, useState, useRef, useId, cloneElement } from 'react'

// Tooltip — lightweight CSS-based tooltip that appears on hover/focus.
// Uses aria-describedby for accessibility: the tooltip element is always
// rendered in the DOM (hidden via CSS when not visible) so the
// aria-describedby reference on the trigger always resolves to a valid
// element. Positioned above the trigger by default. No portal needed —
// uses position: relative on the wrapper.
export interface TooltipProps {
  children: ReactNode
  label: string
  side?: 'top' | 'bottom' | 'right'
  delay?: number
}

export function Tooltip({ children, label, side = 'top', delay = 400 }: TooltipProps) {
  const [visible, setVisible] = useState(false)
  const timerRef = useRef<ReturnType<typeof setTimeout> | null>(null)
  const id = useId()

  const show = () => {
    timerRef.current = setTimeout(() => setVisible(true), delay)
  }

  const hide = () => {
    if (timerRef.current) {
      clearTimeout(timerRef.current)
      timerRef.current = null
    }
    setVisible(false)
  }

  // Clone the child element to attach aria-describedby so screen readers
  // announce the tooltip label. The child is expected to be a single
  // focusable element (button, link, etc.).
  const child = children as ReactElement<{ 'aria-describedby'?: string }>
  const trigger = child && typeof child === 'object' && 'props' in child
    ? cloneElement(child, { 'aria-describedby': id })
    : children

  return (
    <span
      className="if-tooltip-wrapper"
      onMouseEnter={show}
      onMouseLeave={hide}
      onFocus={show}
      onBlur={hide}
    >
      <span
        role="tooltip"
        id={id}
        className={`if-tooltip if-tooltip--${side}${visible ? ' if-tooltip--visible' : ''}`}
      >
        {label}
      </span>
      {trigger}
    </span>
  )
}
