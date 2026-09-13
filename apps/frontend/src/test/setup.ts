import '@testing-library/jest-dom/vitest'
import { cleanup } from '@testing-library/react'
import { afterEach } from 'vitest'

// Auto-cleanup rendered components between tests so DOM from a previous
// test does not leak into the next (e.g. multiple "separator" role elements).
afterEach(() => {
  cleanup()
})

// jsdom does not provide ResizeObserver. The Outliner uses it to measure its
// scroll viewport for virtualization. Provide a minimal mock that calls the
// callback once with a zero-size entry so the effect completes.
const emptySize = [] as unknown as ResizeObserverSize[]
class ResizeObserverMock {
  private callback: ResizeObserverCallback
  constructor(callback: ResizeObserverCallback) {
    this.callback = callback
  }
  observe(target: Element) {
    const entry: ResizeObserverEntry = {
      target,
      contentRect: { width: 0, height: 0, x: 0, y: 0, top: 0, left: 0, bottom: 0, right: 0, toJSON: () => ({}) } as DOMRectReadOnly,
      borderBoxSize: emptySize,
      contentBoxSize: emptySize,
      devicePixelContentBoxSize: emptySize,
    }
    this.callback([entry], this)
  }
  unobserve() {}
  disconnect() {}
}

globalThis.ResizeObserver = ResizeObserverMock as unknown as typeof ResizeObserver
