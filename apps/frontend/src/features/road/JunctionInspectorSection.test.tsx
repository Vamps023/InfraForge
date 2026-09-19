import { describe, it, expect, beforeEach, afterEach } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import { create } from '@bufbuild/protobuf'
import { JunctionInfoSchema } from '@infraforge/protocol'
import { inspectorSectionRegistry } from '../../editor/inspector/inspectorRegistry'
import {
  registerJunctionInspectorSection,
  unregisterJunctionInspectorSection,
} from './JunctionInspectorSection'
import { useRoadStore } from './roadStore'
import type { EngineClient } from '../../lib/engineSession'

describe('JunctionInspectorSection', () => {
  const mockClient = {} as EngineClient

  beforeEach(() => {
    unregisterJunctionInspectorSection()
    useRoadStore.getState().reset()
  })

  afterEach(() => {
    unregisterJunctionInspectorSection()
    useRoadStore.getState().reset()
  })

  it('registers and unregisters junction inspector section in geometry category', () => {
    registerJunctionInspectorSection({ getEngineClient: () => mockClient })
    const section = inspectorSectionRegistry.get('junction')
    expect(section).toBeDefined()
    expect(section?.category).toBe('geometry')
    expect(section?.label).toBe('Junction')

    unregisterJunctionInspectorSection()
    expect(inspectorSectionRegistry.get('junction')).toBeUndefined()
  })

  it('applies only when primaryId starts with junction:', () => {
    registerJunctionInspectorSection({ getEngineClient: () => mockClient })
    const section = inspectorSectionRegistry.get('junction')!

    expect(
      section.applies({
        selectedIds: ['junction:j1'],
        primaryId: 'junction:j1',
      }),
    ).toBe(true)

    expect(
      section.applies({
        selectedIds: ['road:r1'],
        primaryId: 'road:r1',
      }),
    ).toBe(false)
  })

  it('renders junction details from store and allows control type update', async () => {
    const junction = create(JunctionInfoSchema, {
      junctionId: 'junc-test-1',
      name: 'North Intersection',
      type: 'priority',
      posX: 120.5,
      posY: 340.25,
      elevation: 15.0,
      revision: 1n,
      approaches: [],
      connections: [],
    })
    useRoadStore.getState().setJunctions([junction])

    let sentUpdate: any = null
    const client: EngineClient = {
      sendCommand: async (cmd: any) => {
        sentUpdate = cmd
        return {
          case: 'updateJunctionResult',
          value: { junction: { ...junction, type: 'signalized', revision: 2n } },
        }
      },
    } as EngineClient

    registerJunctionInspectorSection({ getEngineClient: () => client })
    const section = inspectorSectionRegistry.get('junction')!

    render(
      <>{section.render({ selectedIds: ['junction:junc-test-1'], primaryId: 'junction:junc-test-1' })}</>,
    )

    expect(screen.getByText('North Intersection')).toBeDefined()
    expect(screen.getByText('junc-test-1')).toBeDefined()
    expect(screen.getByText(/\(120.500, 340.250\) @ 15.000m/)).toBeDefined()

    const select = screen.getByLabelText('Junction control type')
    fireEvent.change(select, { target: { value: 'signalized' } })

    await new Promise((resolve) => setTimeout(resolve, 50))
    expect(sentUpdate).toBeDefined()
    expect(sentUpdate.case).toBe('updateJunction')
    expect(sentUpdate.value.type).toBe('signalized')
  })
})
