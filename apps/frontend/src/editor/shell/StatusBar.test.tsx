import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import { StatusBar } from './StatusBar'
import { useProjectStore } from '../../features/project/projectStore'
import { useViewportStore } from '../../features/viewport/viewportStore'
import type { EngineSessionStatus } from '../../lib/engineSession'
import { create } from '@bufbuild/protobuf'
import { ProjectSummarySchema, type ProjectSummary } from '@infraforge/protocol'
import type { EngineSessionStatus } from '../../lib/engineSession'

const readyEngine: EngineSessionStatus = { state: 'ready', message: 'Engine ready' }
const failedEngine: EngineSessionStatus = { state: 'failed', message: 'Engine failed' }

function makeSummary(): ProjectSummary {
  return create(ProjectSummarySchema, {
    projectUuid: 'test-uuid',
    displayName: 'Test Project',
    directory: '/test',
    revision: 3n,
    dirty: false,
    georeference: { horizontalCrs: 'EPSG:3857', originHeight: 0 },
  })
}

beforeEach(() => {
  useProjectStore.getState().clearProject()
  useViewportStore.getState().setStatus({ state: 'unavailable', detail: 'Not reported' })
})

afterEach(() => {
  useProjectStore.getState().clearProject()
  useViewportStore.getState().setStatus({ state: 'unavailable', detail: 'Not reported' })
})

describe('StatusBar', () => {
  it('renders engine status with state text', () => {
    render(<StatusBar engineStatus={readyEngine} />)
    expect(screen.getByText(/Engine ready/)).toBeInTheDocument()
  })

  it('renders renderer status', () => {
    useViewportStore.getState().setStatus({
      state: 'ready',
      detail: 'Renderer ready',
      gpu: 'Test GPU',
      vulkan: '1.3',
    })
    render(<StatusBar engineStatus={readyEngine} />)
    expect(screen.getByText(/Renderer ready/)).toBeInTheDocument()
    expect(screen.getByText(/Test GPU/)).toBeInTheDocument()
  })

  it('shows no project when no project is open', () => {
    render(<StatusBar engineStatus={readyEngine} />)
    expect(screen.getByText('No project')).toBeInTheDocument()
  })

  it('shows project revision when a project is open', () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<StatusBar engineStatus={readyEngine} />)
    expect(screen.getByText(/Rev 3/)).toBeInTheDocument()
  })

  it('shows CRS when a project is open', () => {
    useProjectStore.getState().setSummary(makeSummary())
    render(<StatusBar engineStatus={readyEngine} />)
    expect(screen.getByText(/EPSG:3857/)).toBeInTheDocument()
  })

  it('shows CRS dash when no project is open', () => {
    render(<StatusBar engineStatus={readyEngine} />)
    expect(screen.getByText('CRS —')).toBeInTheDocument()
  })

  it('renders version label', () => {
    render(<StatusBar engineStatus={readyEngine} />)
    expect(screen.getByText('v0.1.0')).toBeInTheDocument()
  })

  it('does not permanently show camera instructions', () => {
    render(<StatusBar engineStatus={readyEngine} />)
    expect(screen.queryByText(/MMB Pan/)).not.toBeInTheDocument()
  })
})
