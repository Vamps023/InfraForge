import { cleanup, render, screen } from '@testing-library/react'
import userEvent from '@testing-library/user-event'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { NewProjectDialog } from './NewProjectDialog'
import { createProject } from './projectApi'
import { useProjectStore } from './projectStore'
import type { EngineClient } from '../../lib/engineSession'

vi.mock('./projectApi', () => ({ createProject: vi.fn() }))

const client = { request: vi.fn() } as unknown as EngineClient

const GEOGRAPHIC_ENGINE_ERROR =
  "project horizontal CRS 'EPSG:4326' resolves to GEOGRAPHIC_CRS; a projected or engineering CRS with linear axes is required"

afterEach(cleanup)

beforeEach(() => {
  vi.mocked(createProject).mockReset()
  useProjectStore.setState({ lastError: null })
})

async function fillValidForm(user: ReturnType<typeof userEvent.setup>): Promise<HTMLInputElement> {
  await user.type(screen.getByPlaceholderText('Northgate Interchange'), 'Northgate Interchange')
  await user.type(screen.getByPlaceholderText('Parent folder for <name>.iforge'), 'C:\\projects')

  const crsInput = screen.getByRole('combobox', { name: 'Horizontal CRS' }) as HTMLInputElement
  await user.click(crsInput)
  await user.click(screen.getByRole('option', { name: /EPSG:32643/ }))

  const easting = screen.getByLabelText('Origin easting')
  const northing = screen.getByLabelText('Origin northing')
  const height = screen.getByLabelText('Origin height')
  await user.clear(easting)
  await user.type(easting, '100')
  await user.clear(northing)
  await user.type(northing, '200')
  await user.clear(height)
  await user.type(height, '5')
  await user.type(screen.getByLabelText('Vertical CRS (optional)'), 'EPSG:5703')
  await user.selectOptions(screen.getByLabelText('Traffic side'), 'LEFT')
  return crsInput
}

describe('NewProjectDialog horizontal CRS picker', () => {
  it('changing the CRS selection does not reset the other form fields', async () => {
    const user = userEvent.setup()
    render(<NewProjectDialog client={client} busy={false} onClose={vi.fn()} />)
    const crsInput = await fillValidForm(user)

    expect(crsInput).toHaveValue('EPSG:32643')
    await user.click(crsInput)
    await user.click(screen.getByRole('option', { name: /EPSG:32647/ }))
    expect(crsInput).toHaveValue('EPSG:32647')

    expect(screen.getByPlaceholderText('Northgate Interchange')).toHaveValue('Northgate Interchange')
    expect(screen.getByPlaceholderText('Parent folder for <name>.iforge')).toHaveValue('C:\\projects')
    expect(screen.getByLabelText('Origin easting')).toHaveValue('100')
    expect(screen.getByLabelText('Origin northing')).toHaveValue('200')
    expect(screen.getByLabelText('Origin height')).toHaveValue('5')
    expect(screen.getByLabelText('Vertical CRS (optional)')).toHaveValue('EPSG:5703')
    expect(screen.getByLabelText('Traffic side')).toHaveValue('LEFT')
  })

  it('submits the exact selected CRS code to the engine', async () => {
    const user = userEvent.setup()
    vi.mocked(createProject).mockResolvedValueOnce(undefined as never)
    render(<NewProjectDialog client={client} busy={false} onClose={vi.fn()} />)
    await fillValidForm(user)

    await user.click(screen.getByRole('button', { name: 'Create Project' }))
    expect(createProject).toHaveBeenCalledTimes(1)
    const request = vi.mocked(createProject).mock.calls[0]?.[1]
    expect(request?.horizontalCrs).toBe('EPSG:32643')
  })

  it('a geographic CRS still reaches engine validation and is reported with a friendly explanation', async () => {
    const user = userEvent.setup()
    const onClose = vi.fn()
    vi.mocked(createProject).mockRejectedValueOnce(new Error(GEOGRAPHIC_ENGINE_ERROR))
    useProjectStore.setState({
      lastError: { code: 'COMMAND_ERROR_CODE_VALIDATION', message: GEOGRAPHIC_ENGINE_ERROR },
    })

    render(<NewProjectDialog client={client} busy={false} onClose={onClose} />)
    await fillValidForm(user)
    // Switch to the geographic preset exactly as a user would.
    const crsInput = screen.getByRole('combobox', { name: 'Horizontal CRS' }) as HTMLInputElement
    await user.click(crsInput)
    await user.click(screen.getByRole('option', { name: /EPSG:4326/ }))
    expect(crsInput).toHaveValue('EPSG:4326')

    await user.click(screen.getByRole('button', { name: 'Create Project' }))

    // The value reached the engine unconverted; the engine rejected it.
    expect(createProject).toHaveBeenCalledTimes(1)
    expect(vi.mocked(createProject).mock.calls[0]?.[1]?.horizontalCrs).toBe('EPSG:4326')
    expect(onClose).not.toHaveBeenCalled()

    // Friendly explanation on top, engine detail preserved for diagnostics.
    const alert = screen.getByRole('alert')
    expect(alert.textContent).toContain(
      'Project CRS must use linear coordinates such as metres or feet',
    )
    expect(alert.textContent).toContain('latitude/longitude degrees')
    expect(alert.textContent).toContain(GEOGRAPHIC_ENGINE_ERROR)
  })

  it('non-CRS engine errors are shown without the geographic explanation', async () => {
    const user = userEvent.setup()
    vi.mocked(createProject).mockRejectedValueOnce(new Error('parent directory does not exist: C:\\projects'))
    useProjectStore.setState({
      lastError: { code: 'COMMAND_ERROR_CODE_PERSISTENCE_FAILURE', message: 'parent directory does not exist: C:\\projects' },
    })

    render(<NewProjectDialog client={client} busy={false} onClose={vi.fn()} />)
    await fillValidForm(user)
    await user.click(screen.getByRole('button', { name: 'Create Project' }))

    const alert = screen.getByRole('alert')
    expect(alert.textContent).toContain('parent directory does not exist')
    expect(alert.textContent).not.toContain('linear coordinates')
  })
})
