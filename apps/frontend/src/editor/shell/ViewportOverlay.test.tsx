import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import { ViewportOverlay } from './ViewportOverlay'
import { useViewportStore } from '../../features/viewport/viewportStore'

beforeEach(() => {
  useViewportStore.getState().setStatus({ state: 'unavailable', detail: 'Not reported' })
})

afterEach(() => {
  useViewportStore.getState().setStatus({ state: 'unavailable', detail: 'Not reported' })
})

describe('ViewportOverlay', () => {
  it('does not render when viewport is not active', () => {
    const { container } = render(<ViewportOverlay />)
    expect(container.firstChild).toBeNull()
  })

  it('renders renderer state when viewport is ready', () => {
    useViewportStore.getState().setStatus({
      state: 'ready',
      detail: 'Renderer ready',
      gpu: 'Test GPU',
      vulkan: '1.3',
    })
    render(<ViewportOverlay />)
    expect(screen.getByText('ready')).toBeInTheDocument()
    expect(screen.getByText('Test GPU')).toBeInTheDocument()
    expect(screen.getByText('1.3')).toBeInTheDocument()
  })

  it('does not render when viewport is suspended', () => {
    useViewportStore.getState().setStatus({ state: 'suspended', detail: 'Suspended' })
    const { container } = render(<ViewportOverlay />)
    expect(container.firstChild).toBeNull()
  })

  it('renders without GPU when not provided', () => {
    useViewportStore.getState().setStatus({
      state: 'ready',
      detail: 'Renderer ready',
    })
    render(<ViewportOverlay />)
    expect(screen.getByText('ready')).toBeInTheDocument()
    expect(screen.queryByText('GPU')).not.toBeInTheDocument()
  })
})
