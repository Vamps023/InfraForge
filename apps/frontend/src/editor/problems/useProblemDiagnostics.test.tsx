import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { render, act } from '@testing-library/react'
import { useProblemDiagnostics, PROBLEM_DIAGNOSTIC_IDS } from './useProblemDiagnostics'
import { useProblemsStore } from './problemsStore'
import { useViewportStore } from '../../features/viewport/viewportStore'
import { useProjectStore } from '../../features/project/projectStore'

function renderHook() {
  let result: { current: void } = { current: undefined }
  function Hook() {
    result.current = useProblemDiagnostics()
    return null
  }
  render(<Hook />)
  return result
}

beforeEach(() => {
  useProblemsStore.getState().clear()
  useViewportStore.setState({
    status: { state: 'ready', detail: 'Viewport ready.' },
  })
  useProjectStore.getState().setLastError(null)
})

afterEach(() => {
  useProblemsStore.getState().clear()
  useViewportStore.setState({
    status: { state: 'ready', detail: 'Viewport ready.' },
  })
  useProjectStore.getState().setLastError(null)
})

describe('useProblemDiagnostics viewport lifecycle', () => {
  it('failed A → failed B → recovered leaves zero viewport diagnostics', () => {
    renderHook()

    // Failed A.
    act(() => {
      useViewportStore.setState({
        status: { state: 'failed', detail: 'GPU lost' },
      })
    })
    let viewportDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'viewport')
    expect(viewportDiags).toHaveLength(1)
    expect(viewportDiags[0]!.id).toBe(PROBLEM_DIAGNOSTIC_IDS.viewportStatus)
    expect(viewportDiags[0]!.message).toContain('GPU lost')

    // Failed B — replaces A (same stable id), no accumulation.
    act(() => {
      useViewportStore.setState({
        status: { state: 'failed', detail: 'Surface lost' },
      })
    })
    viewportDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'viewport')
    expect(viewportDiags).toHaveLength(1)
    expect(viewportDiags[0]!.message).toContain('Surface lost')

    // Stopped — still a failure, replaces B.
    act(() => {
      useViewportStore.setState({
        status: { state: 'stopped', detail: 'Process exited' },
      })
    })
    viewportDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'viewport')
    expect(viewportDiags).toHaveLength(1)
    expect(viewportDiags[0]!.message).toContain('Process exited')

    // Recovered — all viewport diagnostics removed.
    act(() => {
      useViewportStore.setState({
        status: { state: 'ready', detail: 'Viewport ready.' },
      })
    })
    viewportDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'viewport')
    expect(viewportDiags).toHaveLength(0)
  })

  it('does not accumulate when failure detail changes repeatedly', () => {
    renderHook()
    for (let i = 0; i < 5; i += 1) {
      act(() => {
        useViewportStore.setState({
          status: { state: 'failed', detail: `failure ${i}` },
        })
      })
    }
    const viewportDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'viewport')
    expect(viewportDiags).toHaveLength(1)
  })

  it('project command errors follow predictable source replacement', () => {
    renderHook()

    act(() => {
      useProjectStore.getState().setLastError({ code: '1', message: 'project error A' })
    })
    let projectDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'project')
    expect(projectDiags).toHaveLength(1)
    expect(projectDiags[0]!.id).toBe(PROBLEM_DIAGNOSTIC_IDS.projectCommand)
    expect(projectDiags[0]!.message).toBe('project error A')

    act(() => {
      useProjectStore.getState().setLastError({ code: '2', message: 'project error B' })
    })
    projectDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'project')
    expect(projectDiags).toHaveLength(1)
    expect(projectDiags[0]!.message).toBe('project error B')

    act(() => {
      useProjectStore.getState().setLastError(null)
    })
    projectDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'project')
    expect(projectDiags).toHaveLength(0)
  })

  it('does not classify project command errors as engine source', () => {
    renderHook()
    act(() => {
      useProjectStore.getState().setLastError({ code: '1', message: 'project error' })
    })
    const engineDiags = useProblemsStore
      .getState()
      .diagnostics.filter((d) => d.source === 'engine')
    expect(engineDiags).toHaveLength(0)
  })
})
