import { beforeEach, describe, expect, it } from 'vitest'
import { inspectorSectionRegistry, type InspectorSection } from './inspectorRegistry'

function makeSection(id: string, overrides: Partial<InspectorSection> = {}): InspectorSection {
  return {
    id,
    label: id,
    applies: () => true,
    render: () => null,
    ...overrides,
  }
}

beforeEach(() => {
  for (const section of inspectorSectionRegistry.all()) {
    inspectorSectionRegistry.unregister(section.id)
  }
})

describe('inspectorSectionRegistry', () => {
  it('registers and resolves sections', () => {
    inspectorSectionRegistry.register(makeSection('a'))
    const resolved = inspectorSectionRegistry.resolve({ selectedIds: [], primaryId: null })
    expect(resolved.map((s) => s.id)).toEqual(['a'])
  })

  it('rejects duplicate section ids', () => {
    inspectorSectionRegistry.register(makeSection('dup'))
    expect(() => inspectorSectionRegistry.register(makeSection('dup'))).toThrowError(
      /Duplicate inspector section id/,
    )
  })

  it('unregisters a section', () => {
    inspectorSectionRegistry.register(makeSection('gone'))
    inspectorSectionRegistry.unregister('gone')
    expect(inspectorSectionRegistry.resolve({ selectedIds: [], primaryId: null })).toEqual([])
  })

  it('resolves only sections that apply to the current selection', () => {
    inspectorSectionRegistry.register(
      makeSection('empty-only', { applies: (ctx) => ctx.selectedIds.length === 0 }),
    )
    inspectorSectionRegistry.register(
      makeSection('has-selection', { applies: (ctx) => ctx.selectedIds.length > 0 }),
    )
    expect(
      inspectorSectionRegistry.resolve({ selectedIds: [], primaryId: null }).map((s) => s.id),
    ).toEqual(['empty-only'])
    expect(
      inspectorSectionRegistry.resolve({ selectedIds: ['x'], primaryId: 'x' }).map((s) => s.id),
    ).toEqual(['has-selection'])
  })

  it('orders sections by the order hint', () => {
    inspectorSectionRegistry.register(makeSection('late', { order: 200 }))
    inspectorSectionRegistry.register(makeSection('early', { order: 10 }))
    inspectorSectionRegistry.register(makeSection('mid', { order: 100 }))
    expect(
      inspectorSectionRegistry.resolve({ selectedIds: [], primaryId: null }).map((s) => s.id),
    ).toEqual(['early', 'mid', 'late'])
  })

  it('defaults order to 100', () => {
    inspectorSectionRegistry.register(makeSection('default', { order: 100 }))
    inspectorSectionRegistry.register(makeSection('zero', { order: 0 }))
    expect(
      inspectorSectionRegistry.resolve({ selectedIds: [], primaryId: null }).map((s) => s.id),
    ).toEqual(['zero', 'default'])
  })

  it('orders sections by standard 8-tier category order', () => {
    inspectorSectionRegistry.register(makeSection('diag', { category: 'diagnostics' }))
    inspectorSectionRegistry.register(makeSection('geom', { category: 'geometry' }))
    inspectorSectionRegistry.register(makeSection('ident', { category: 'identity' }))
    inspectorSectionRegistry.register(makeSection('src', { category: 'source' }))
    expect(
      inspectorSectionRegistry.resolve({ selectedIds: [], primaryId: null }).map((s) => s.id),
    ).toEqual(['ident', 'geom', 'src', 'diag'])
  })
})

