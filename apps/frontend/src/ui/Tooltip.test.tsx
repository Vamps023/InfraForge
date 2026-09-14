import { describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import { Tooltip } from './Tooltip'

describe('Tooltip', () => {
  it('attaches aria-describedby to the trigger element', () => {
    render(
      <Tooltip label="Import a file">
        <button type="button">Import</button>
      </Tooltip>,
    )
    const trigger = screen.getByText('Import')
    const describedBy = trigger.getAttribute('aria-describedby')
    expect(describedBy).toBeTruthy()
    // The tooltip element with that id must exist in the DOM.
    const tooltipElement = document.getElementById(describedBy!)
    expect(tooltipElement).not.toBeNull()
    expect(tooltipElement).toHaveTextContent('Import a file')
    expect(tooltipElement).toHaveAttribute('role', 'tooltip')
  })

  it('renders the tooltip element in the DOM even when not visible', () => {
    render(
      <Tooltip label="Import a file">
        <button type="button">Import</button>
      </Tooltip>,
    )
    // The tooltip text is always in the DOM (hidden via CSS) so
    // aria-describedby always references a valid element.
    expect(screen.getByText('Import a file')).toBeInTheDocument()
  })
})
