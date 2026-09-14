import { type ReactNode, type ButtonHTMLAttributes } from 'react'

// Button — standard button with primary/default variants. Uses semantic
// design tokens. Supports icon + label composition.
export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: 'default' | 'primary'
  children: ReactNode
}

export function Button({
  variant = 'default',
  children,
  className = '',
  ...rest
}: ButtonProps) {
  const cls = `button${variant === 'primary' ? ' primary' : ''}${className ? ` ${className}` : ''}`
  return (
    <button type="button" className={cls} {...rest}>
      {children}
    </button>
  )
}
