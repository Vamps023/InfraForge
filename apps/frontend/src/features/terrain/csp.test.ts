import { describe, it, expect } from 'vitest'
// Vite ?raw import returns the file contents as a string at build/test time.
import indexHtml from '../../../index.html?raw'

// CSP regression test for the packaged Electron renderer (Issue #6).
// The Download Area map loads remote HTTPS tile images (e.g.
// https://tile.openstreetmap.org/{z}/{x}/{y}.png). The renderer CSP must
// permit remote HTTPS images while keeping scripts restricted to 'self'
// so arbitrary remote scripts cannot execute in the desktop app.

function extractCsp(html: string): string {
  // The content attribute is double-quoted and contains single quotes
  // (e.g. 'self'), so capture everything up to the closing double quote.
  const match = html.match(
    /<meta[^>]+http-equiv=["']?Content-Security-Policy["']?[^>]+content="([^"]+)"/i,
  )
  return match ? match[1]! : ''
}

// Extract a single CSP directive value (e.g. "img-src 'self' data: https:")
// and assert it is present so callers get a defined string.
function directive(csp: string, name: string): string {
  const re = new RegExp(`${name}\\s+([^;]+)`, 'i')
  const match = csp.match(re)
  if (!match || match[1] === undefined) {
    throw new Error(`CSP directive "${name}" not found`)
  }
  return match[1]
}

describe('Renderer Content-Security-Policy (Issue #6 map tiles)', () => {
  const csp = extractCsp(indexHtml)

  it('declares a Content-Security-Policy meta tag', () => {
    expect(csp.length).toBeGreaterThan(0)
  })

  it('permits remote HTTPS map tile images (img-src includes https:)', () => {
    const imgSrc = directive(csp, 'img-src')
    expect(imgSrc).toContain('https:')
    // Local assets and data: URIs must remain permitted.
    expect(imgSrc).toContain("'self'")
    expect(imgSrc).toContain('data:')
  })

  it('keeps scripts restricted to self (no remote scripts)', () => {
    const scriptSrc = directive(csp, 'script-src')
    expect(scriptSrc.trim()).toBe("'self'")
    // No https: / * / unsafe-eval in script-src.
    expect(scriptSrc).not.toContain('https:')
    expect(scriptSrc).not.toContain('*')
    expect(scriptSrc).not.toContain("'unsafe-eval'")
  })

  it('does not allow arbitrary remote connections from the renderer', () => {
    // Geocoder requests route through Electron main IPC, not renderer fetch.
    const connectSrc = directive(csp, 'connect-src')
    // connect-src must not contain https: (no arbitrary remote fetch from
    // the renderer; the geocoder is proxied through Electron main IPC).
    expect(connectSrc).not.toContain('https:')
    // No bare host wildcard '*' as a source (a port wildcard like
    // ws://127.0.0.1:* for the local engine socket is acceptable).
    expect(connectSrc).not.toMatch(/(^|\s)\*(\s|$)/)
  })

  it('blocks plugins and framing (object-src none, frame-ancestors none)', () => {
    expect(csp).toMatch(/object-src\s+[^;]*'none'/i)
    expect(csp).toMatch(/frame-ancestors\s+[^;]*'none'/i)
  })
})
