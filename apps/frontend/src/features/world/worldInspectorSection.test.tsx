import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { inspectorSectionRegistry } from '../../editor/inspector/inspectorRegistry'
import {
  registerWorldInspectorSection,
  unregisterWorldInspectorSection,
} from './worldInspectorSection'
import { useProjectStore } from '../project/projectStore'

describe('worldInspectorSection', () => {
  beforeEach(() => {
    unregisterWorldInspectorSection()
    useProjectStore.getState().clearProject()
  })

  afterEach(() => {
    unregisterWorldInspectorSection()
    useProjectStore.getState().clearProject()
  })

  it('registers and unregisters world-georeference section in geometry category', () => {
    registerWorldInspectorSection()
    const section = inspectorSectionRegistry.get('world-georeference')
    expect(section).toBeDefined()
    expect(section?.category).toBe('geometry')
    expect(section?.label).toBe('Spatial Reference & Georeference')

    unregisterWorldInspectorSection()
    expect(inspectorSectionRegistry.get('world-georeference')).toBeUndefined()
  })

  it('applies when no entity is selected', () => {
    registerWorldInspectorSection()
    const section = inspectorSectionRegistry.get('world-georeference')!
    expect(
      section.applies({
        selectedIds: [],
        primaryId: null,
      }),
    ).toBe(true)
  })

  it('does not apply when a road is selected', () => {
    registerWorldInspectorSection()
    const section = inspectorSectionRegistry.get('world-georeference')!
    expect(
      section.applies({
        selectedIds: ['road:road-1'],
        primaryId: 'road:road-1',
      }),
    ).toBe(false)
  })
})
