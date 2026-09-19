import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { render, screen } from '@testing-library/react'
import { create } from '@bufbuild/protobuf'
import { RoadSummarySchema } from '@infraforge/protocol'
import { inspectorSectionRegistry } from '../../editor/inspector/inspectorRegistry'
import {
  registerRoadInspectorSection,
  unregisterRoadInspectorSection,
} from './roadInspectorSection'
import { useRoadStore } from './roadStore'
import type { EngineClient } from '../../lib/engineSession'

describe('roadInspectorSection', () => {
  const mockClient = {} as EngineClient

  beforeEach(() => {
    unregisterRoadInspectorSection()
    useRoadStore.getState().reset()
  })

  afterEach(() => {
    unregisterRoadInspectorSection()
    useRoadStore.getState().reset()
  })

  it('registers and unregisters road inspector section in geometry category', () => {
    registerRoadInspectorSection({ getEngineClient: () => mockClient })
    const section = inspectorSectionRegistry.get('road')
    expect(section).toBeDefined()
    expect(section?.category).toBe('geometry')
    expect(section?.label).toBe('Road')

    unregisterRoadInspectorSection()
    expect(inspectorSectionRegistry.get('road')).toBeUndefined()
  })

  it('applies only when primaryId starts with road:', () => {
    registerRoadInspectorSection({ getEngineClient: () => mockClient })
    const section = inspectorSectionRegistry.get('road')!

    expect(
      section.applies({
        selectedIds: ['road:abc'],
        primaryId: 'road:abc',
      }),
    ).toBe(true)

    expect(
      section.applies({
        selectedIds: ['terrain:abc'],
        primaryId: 'terrain:abc',
      }),
    ).toBe(false)

    expect(
      section.applies({
        selectedIds: [],
        primaryId: null,
      }),
    ).toBe(false)
  })

  it('renders road details and visibly displays lastError when present', () => {
    registerRoadInspectorSection({ getEngineClient: () => mockClient })
    const section = inspectorSectionRegistry.get('road')!

    useRoadStore.getState().upsertRoad(
      create(RoadSummarySchema, {
        roadId: 'test-road-1',
        name: 'Highway 101',
        length: 1250.5,
        alignmentSegmentCount: 3,
        protectedAnchorCount: 1,
        revision: 4n,
      }),
    )

    const { rerender } = render(
      <div>
        {section.render({
          selectedIds: ['road:test-road-1'],
          primaryId: 'road:test-road-1',
        })}
      </div>,
    )

    expect(screen.getByText('Highway 101')).toBeInTheDocument()
    expect(screen.getByText('test-road-1')).toBeInTheDocument()
    expect(screen.queryByRole('alert')).not.toBeInTheDocument()

    // Set lastError
    useRoadStore.getState().setLastError('Alignment fitting failed: degenerate control point')

    rerender(
      <div>
        {section.render({
          selectedIds: ['road:test-road-1'],
          primaryId: 'road:test-road-1',
        })}
      </div>,
    )

    const alert = screen.getByRole('alert')
    expect(alert).toBeInTheDocument()
    expect(alert).toHaveTextContent('Alignment fitting failed: degenerate control point')
  })
})
