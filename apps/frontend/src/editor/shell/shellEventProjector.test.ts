import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { create } from '@bufbuild/protobuf'
import {
  EventEnvelopeSchema,
  OperationEventSchema,
  DiagnosticEventSchema,
  type EventEnvelope,
} from '@infraforge/protocol'
import { applyShellEvent } from './shellEventProjector'
import { useOperationsStore } from '../operations/operationsStore'
import { useProblemsStore } from '../problems/problemsStore'

function makeOperationEvent(
  operationId = 'op-1',
  name = 'Terrain import',
  source = 'terrain',
  state: number = 2,
  progress = 0.5,
  cancellable = false,
  message = '',
  targetId = '',
): EventEnvelope {
  const operation = create(OperationEventSchema, {
    operationId,
    name,
    source,
    state,
    progress,
    cancellable,
    message,
    targetId,
  })
  return create(EventEnvelopeSchema, {
    eventId: 'evt-1',
    event: { case: 'operation', value: operation },
  })
}

function makeDiagnosticEvent(
  diagnosticId = 'diag-1',
  severity: number = 3,
  message = 'Validation failed',
  source = 'terrain-validation',
  targetId = '',
): EventEnvelope {
  const diagnostic = create(DiagnosticEventSchema, {
    diagnosticId,
    severity,
    message,
    source,
    targetId,
  })
  return create(EventEnvelopeSchema, {
    eventId: 'evt-2',
    event: { case: 'diagnostic', value: diagnostic },
  })
}

beforeEach(() => {
  useOperationsStore.getState().clear()
  useProblemsStore.getState().clear()
})

afterEach(() => {
  useOperationsStore.getState().clear()
  useProblemsStore.getState().clear()
})

describe('applyShellEvent', () => {
  it('projects an operation event into the operations store', () => {
    applyShellEvent(makeOperationEvent())
    const ops = useOperationsStore.getState().operations
    expect(ops).toHaveLength(1)
    expect(ops[0]!.id).toBe('op-1')
    expect(ops[0]!.name).toBe('Terrain import')
    expect(ops[0]!.state).toBe('running')
    expect(ops[0]!.progress).toBe(0.5)
    expect(ops[0]!.source).toBe('terrain')
    expect(ops[0]!.cancellable).toBe(false)
  })

  it('updates an existing operation when the same ID is received again', () => {
    applyShellEvent(makeOperationEvent('op-1', 'Terrain import', 'terrain', 2, 0.3))
    applyShellEvent(makeOperationEvent('op-1', 'Terrain import', 'terrain', 3, 1.0))
    const ops = useOperationsStore.getState().operations
    expect(ops).toHaveLength(1)
    expect(ops[0]!.state).toBe('completed')
    expect(ops[0]!.progress).toBe(1.0)
  })

  it('projects a diagnostic event into the problems store', () => {
    applyShellEvent(makeDiagnosticEvent())
    const diags = useProblemsStore.getState().diagnostics
    expect(diags).toHaveLength(1)
    expect(diags[0]!.id).toBe('diag-1')
    expect(diags[0]!.severity).toBe('error')
    expect(diags[0]!.message).toBe('Validation failed')
    expect(diags[0]!.source).toBe('terrain-validation')
  })

  it('updates an existing diagnostic when the same ID is received again', () => {
    applyShellEvent(makeDiagnosticEvent('diag-1', 3, 'First error'))
    applyShellEvent(makeDiagnosticEvent('diag-1', 3, 'Updated error'))
    const diags = useProblemsStore.getState().diagnostics
    expect(diags).toHaveLength(1)
    expect(diags[0]!.message).toBe('Updated error')
  })

  it('ignores unknown event cases', () => {
    const event = create(EventEnvelopeSchema, {
      eventId: 'evt-3',
      event: { case: 'projectOpened', value: {} as never },
    })
    applyShellEvent(event)
    expect(useOperationsStore.getState().operations).toHaveLength(0)
    expect(useProblemsStore.getState().diagnostics).toHaveLength(0)
  })

  it('maps operation states correctly', () => {
    const states: Array<[number, string]> = [
      [1, 'pending'],
      [2, 'running'],
      [3, 'completed'],
      [4, 'failed'],
      [5, 'cancelled'],
    ]
    for (const [protoState, expectedState] of states) {
      useOperationsStore.getState().clear()
      applyShellEvent(makeOperationEvent('op-1', 'Test', 'test', protoState))
      expect(useOperationsStore.getState().operations[0]!.state).toBe(expectedState)
    }
  })

  it('maps diagnostic severities correctly', () => {
    const severities: Array<[number, 'info' | 'warning' | 'error']> = [
      [1, 'info'],
      [2, 'warning'],
      [3, 'error'],
    ]
    for (const [protoSeverity, expectedSeverity] of severities) {
      useProblemsStore.getState().clear()
      applyShellEvent(makeDiagnosticEvent('diag-1', protoSeverity))
      expect(useProblemsStore.getState().diagnostics[0]!.severity).toBe(expectedSeverity)
    }
  })

  it('stores start empty — no fabricated events', () => {
    expect(useOperationsStore.getState().operations).toHaveLength(0)
    expect(useProblemsStore.getState().diagnostics).toHaveLength(0)
  })
})
