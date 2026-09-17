import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { render, renderHook } from '@testing-library/react'
import { useViewportHost } from './useViewportHost'
import { useViewportStore } from './viewportStore'
import { useRef } from 'react'

// useViewportHost reports the viewport-host rectangle to the desktop shell
// and, critically, reports the page's combined desired visibility over the
// viewport:set-visible channel. The native viewport is a child HWND that
// CSS cannot occlude, so when a blocking overlay (dialog OR the no-project
// home screen) is active, the page must tell the desktop shell to hide the
// native surface. These tests verify that contract.

interface DesktopBridgeMock {
  setViewportBounds: ReturnType<typeof vi.fn>
  setViewportVisible: ReturnType<typeof vi.fn>
  onViewportStatus: ReturnType<typeof vi.fn>
}

function installDesktopBridge(): DesktopBridgeMock {
  const mock: DesktopBridgeMock = {
    setViewportBounds: vi.fn(),
    setViewportVisible: vi.fn(),
    onViewportStatus: vi.fn(() => () => {}),
  }
  // jsdom does not implement matchMedia; useViewportHost uses it to watch
  // device-pixel-ratio changes. Provide a minimal stub.
  const matchMediaStub = vi.fn().mockReturnValue({
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  })
  vi.stubGlobal('window', {
    ...window,
    infraforgeDesktop: {
      setViewportBounds: mock.setViewportBounds,
      setViewportVisible: mock.setViewportVisible,
      onViewportStatus: mock.onViewportStatus,
    },
    matchMedia: matchMediaStub,
  })
  return mock
}

function clearDesktopBridge(): void {
  vi.unstubAllGlobals()
}

beforeEach(() => {
  useViewportStore.getState().setStatus({ state: 'unavailable', detail: 'reset' })
})

afterEach(() => {
  clearDesktopBridge()
})

describe('useViewportHost visibility', () => {
  it('reports viewport visible when no overlay is active', () => {
    const mock = installDesktopBridge()
    const hostRef = { current: null } as React.RefObject<HTMLDivElement | null>
    renderHook(() => useViewportHost(hostRef, { blockedByOverlay: false }))
    expect(mock.setViewportVisible).toHaveBeenCalledWith(true)
  })

  it('reports viewport hidden when a blocking overlay is active', () => {
    const mock = installDesktopBridge()
    const hostRef = { current: null } as React.RefObject<HTMLDivElement | null>
    renderHook(() => useViewportHost(hostRef, { blockedByOverlay: true }))
    expect(mock.setViewportVisible).toHaveBeenCalledWith(false)
  })

  it('re-asserts visibility when blockedByOverlay toggles', () => {
    const mock = installDesktopBridge()
    const hostRef = { current: null } as React.RefObject<HTMLDivElement | null>
    const { rerender } = renderHook(
      ({ blocked }: { blocked: boolean }) => useViewportHost(hostRef, { blockedByOverlay: blocked }),
      { initialProps: { blocked: false } },
    )
    expect(mock.setViewportVisible).toHaveBeenLastCalledWith(true)
    rerender({ blocked: true })
    expect(mock.setViewportVisible).toHaveBeenLastCalledWith(false)
    rerender({ blocked: false })
    expect(mock.setViewportVisible).toHaveBeenLastCalledWith(true)
  })

  it('reports viewport hidden on document hide and restores when unhidden', () => {
    const mock = installDesktopBridge()
    const hostRef = { current: null } as React.RefObject<HTMLDivElement | null>
    renderHook(() => useViewportHost(hostRef, { blockedByOverlay: false }))
    expect(mock.setViewportVisible).toHaveBeenLastCalledWith(true)

    // Simulate document becoming hidden (e.g. window minimized or backgrounded)
    Object.defineProperty(document, 'hidden', { configurable: true, value: true })
    document.dispatchEvent(new Event('visibilitychange'))
    expect(mock.setViewportVisible).toHaveBeenLastCalledWith(false)

    // Restore document visibility
    Object.defineProperty(document, 'hidden', { configurable: true, value: false })
    document.dispatchEvent(new Event('visibilitychange'))
    expect(mock.setViewportVisible).toHaveBeenLastCalledWith(true)
  })
})

// Integration-style test: a host element with a real ref receives bounds.
describe('useViewportHost bounds', () => {
  it('forwards host bounds to the desktop shell on layout', () => {
    const mock = installDesktopBridge()
    function Probe() {
      const ref = useRef<HTMLDivElement | null>(null)
      useViewportHost(ref, { blockedByOverlay: false })
      return <div ref={ref} />
    }
    render(<Probe />)
    // jsdom does not perform real layout, so we only assert that bounds
    // were forwarded (the ResizeObserver mock fires once on observe). The
    // exact rect values are not meaningful without a layout engine.
    expect(mock.setViewportBounds).toHaveBeenCalled()
    const callArgs = mock.setViewportBounds.mock.calls[0]!
    expect(callArgs[0]).toEqual(
      expect.objectContaining({
        x: expect.any(Number),
        y: expect.any(Number),
        width: expect.any(Number),
        height: expect.any(Number),
      }),
    )
  })
})
