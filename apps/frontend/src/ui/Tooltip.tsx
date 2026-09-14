import { type ReactNode, useState, useRef, useId } from 'react'

// Tooltip — lightweight CSS-based tooltip that appears on hover/focus.
// Uses aria-describedby for accessibility. Positioned above the trigger
// by default. No portal needed — uses position: relative on the wrapper.
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

  return (
    <span
      className="if-tooltip-wrapper"
      onMouseEnter={show}
      onMouseLeave={hide}
      onFocus={show}
      onBlur={hide}
    >
      {visible ? (
        <span
          role="tooltip"
          id={id}
          className={`if-tooltip if-tooltip--${side}`}
        >
          {label}
        </span>
      ) : null}
      {children}
    </span>
  )
}
