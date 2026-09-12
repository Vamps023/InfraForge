import { cleanup, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, describe, expect, it } from 'vitest'
import { useState } from 'react'
import { CrsPicker } from './CrsPicker'

afterEach(cleanup)

// Mirrors how the New Project dialog consumes the picker: the dialog owns
// the horizontalCrs string, the picker only reports what was chosen.
function Harness() {
  const [value, setValue] = useState('')
  return <CrsPicker value={value} onChange={setValue} />
}

async function openPicker(user: ReturnType<typeof userEvent.setup>) {
  const input = screen.getByRole('combobox', { name: 'Horizontal CRS' })
  await user.click(input)
  return input
}

describe('CrsPicker', () => {
  it('renders the Common Global and Common UTM groups when opened', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    await openPicker(user)

    expect(screen.getByText('Common Global CRSs')).toBeTruthy()
    expect(screen.getByText('Common UTM CRSs')).toBeTruthy()
    expect(screen.getAllByRole('option').length).toBeGreaterThanOrEqual(12)
  })

  it('shows EPSG:4326 as Geographic and not valid as Project CRS', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    await openPicker(user)

    const option = screen.getByRole('option', { name: /EPSG:4326/ })
    expect(option.textContent).toContain('WGS 84 - Geographic')
    expect(option.textContent).toContain('Geographic · degrees')
    expect(option.textContent).toContain('Not valid as Project CRS')
  })

  it('shows EPSG:3857 as Projected · metre', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    await openPicker(user)

    const option = screen.getByRole('option', { name: /EPSG:3857/ })
    expect(option.textContent).toContain('WGS 84 / Pseudo-Mercator')
    expect(option.textContent).toContain('Projected · metre')
  })

  it('lists the common UTM zones', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    await openPicker(user)

    for (const zone of [32, 33, 34, 43, 44, 45, 46, 47, 48]) {
      expect(screen.getByRole('option', { name: new RegExp(`EPSG:326${zone}`) })).toBeTruthy()
    }
  })

  it('filters by EPSG code while typing', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    const input = await openPicker(user)

    await user.type(input, '4326')
    expect(screen.getByRole('option', { name: /EPSG:4326/ })).toBeTruthy()
    expect(screen.queryByRole('option', { name: /EPSG:32643/ })).toBeNull()
  })

  it('filters by name ("WGS 84") and UTM zone text ("UTM 43N")', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    const input = await openPicker(user)

    await user.type(input, 'UTM 43N')
    expect(screen.getByRole('option', { name: /EPSG:32643/ })).toBeTruthy()
    expect(screen.queryByRole('option', { name: /EPSG:32644/ })).toBeNull()

    await user.clear(input)
    await user.type(input, 'WGS 84')
    expect(screen.getByRole('option', { name: /EPSG:4326/ })).toBeTruthy()
    expect(screen.getByRole('option', { name: /EPSG:32648/ })).toBeTruthy()
  })

  it('writes exactly the chosen code into the field (EPSG:32643)', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    const input = await openPicker(user)

    await user.click(screen.getByRole('option', { name: /EPSG:32643 \(WGS 84 \/ UTM Zone 43N\)/ }))
    expect(input).toHaveValue('EPSG:32643')
    expect(screen.queryByRole('listbox', { name: 'Common CRS presets' })).toBeNull()
  })

  it('selecting the geographic EPSG:4326 writes it verbatim without converting', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    const input = await openPicker(user)

    await user.click(screen.getByRole('option', { name: /EPSG:4326/ }))
    expect(input).toHaveValue('EPSG:4326')
    // No silent substitution to EPSG:3857 or any UTM zone.
    expect(input.getAttribute('value')).not.toContain('3857')
    // The caption explains the preset is invalid as a project CRS.
    expect(screen.getByText(/Not valid as Project CRS/)).toBeTruthy()
  })

  it('keeps manual free-text entry possible', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    const input = await openPicker(user)

    await user.type(input, 'EPSG:25832')
    expect(input).toHaveValue('EPSG:25832')

    await user.click(screen.getByRole('option', { name: /Enter CRS manually/ }))
    expect(input).toHaveValue('EPSG:25832')
    expect(screen.queryByRole('listbox', { name: 'Common CRS presets' })).toBeNull()
  })

  it('supports keyboard navigation: arrows move, Enter selects, Escape closes', async () => {
    const user = userEvent.setup()
    render(<Harness />)
    const input = screen.getByRole('combobox', { name: 'Horizontal CRS' })
    await user.click(input)
    expect(input.getAttribute('aria-expanded')).toBe('true')

    // One arrow moves from the initially-active first option to the second.
    await user.keyboard('{ArrowDown}')
    const secondOption = screen.getByRole('option', { name: /EPSG:3857/ })
    expect(input.getAttribute('aria-activedescendant')).toBe(secondOption.id)

    await user.keyboard('{Enter}')
    expect(input).toHaveValue('EPSG:3857')
    expect(input.getAttribute('aria-expanded')).toBe('false')

    await user.click(input)
    await user.keyboard('{Escape}')
    expect(input.getAttribute('aria-expanded')).toBe('false')
  })
})
