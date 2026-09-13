import { beforeEach, describe, expect, it } from 'vitest'
import {
  useProblemsStore,
  engineSessionDiagnostic,
  viewportDiagnostic,
  type DiagnosticProjection,
} from './problemsStore'

function diag(id: string, overrides: Partial<DiagnosticProjection> = {}): DiagnosticProjection {
  return {
    id,
    severity: 'error',
    message: id,
    source: 'test',
    ...overrides,
  }
}

beforeEach(() => {
  useProblemsStore.getState().clear()
})

describe('problemsStore', () => {
  it('starts empty with no sample warnings', () => {
    expect(useProblemsStore.getState().diagnostics).toEqual([])
  })

  it('upserts a new diagnostic', () => {
    useProblemsStore.getState().upsert(diag('d:1'))
    expect(useProblemsStore.getState().diagnostics).toHaveLength(1)
  })

  it('updates an existing diagnostic by id', () => {
    useProblemsStore.getState().upsert(diag('d:1', { message: 'first' }))
    useProblemsStore.getState().upsert(diag('d:1', { message: 'second' }))
    expect(useProblemsStore.getState().diagnostics[0]!.message).toBe('second')
  })

  it('removes a diagnostic by id', () => {
    useProblemsStore.getState().upsert(diag('d:1'))
    useProblemsStore.getState().remove('d:1')
    expect(useProblemsStore.getState().diagnostics).toEqual([])
  })

  it('clears all diagnostics', () => {
    useProblemsStore.getState().upsert(diag('d:1'))
    useProblemsStore.getState().upsert(diag('d:2'))
    useProblemsStore.getState().clear()
    expect(useProblemsStore.getState().diagnostics).toEqual([])
  })

  it('clears diagnostics for a single source', () => {
    useProblemsStore.getState().upsert(diag('d:1', { source: 'engine' }))
    useProblemsStore.getState().upsert(diag('d:2', { source: 'viewport' }))
    useProblemsStore.getState().clearSource('engine')
    expect(useProblemsStore.getState().diagnostics.map((d) => d.id)).toEqual(['d:2'])
  })

  it('replaces all diagnostics for a source', () => {
    useProblemsStore.getState().upsert(diag('d:1', { source: 'engine' }))
    useProblemsStore.getState().upsert(diag('d:2', { source: 'engine' }))
    useProblemsStore.getState().upsert(diag('d:3', { source: 'viewport' }))
    useProblemsStore.getState().replaceSource('engine', [diag('d:4', { source: 'engine' })])
    const ids = useProblemsStore.getState().diagnostics.map((d) => d.id)
    expect(ids).toContain('d:3')
    expect(ids).toContain('d:4')
    expect(ids).not.toContain('d:1')
    expect(ids).not.toContain('d:2')
  })

  it('carries a canonical object id for navigation hooks', () => {
    useProblemsStore.getState().upsert(diag('d:1', { targetId: 'road:1' }))
    expect(useProblemsStore.getState().diagnostics[0]!.targetId).toBe('road:1')
  })
})

describe('problemsStore diagnostic factories', () => {
  it('engineSessionDiagnostic produces a stable engine-source diagnostic', () => {
    const d = engineSessionDiagnostic('error', 'engine failed')
    expect(d.source).toBe('engine')
    expect(d.severity).toBe('error')
    expect(d.message).toBe('engine failed')
    // Stable id derived from the message so re-upserts replace, not duplicate.
    expect(d.id).toBe(`engine-session:engine failed`)
  })

  it('viewportDiagnostic produces a stable viewport-source diagnostic', () => {
    const d = viewportDiagnostic('warning', 'viewport suspended')
    expect(d.source).toBe('viewport')
    expect(d.id).toBe(`viewport:viewport suspended`)
  })
})
