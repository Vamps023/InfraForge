import { beforeEach, describe, expect, it } from 'vitest'
import {
  useOperationsStore,
  activeOperations,
  recentOperations,
  type OperationProjection,
} from './operationsStore'

function op(id: string, overrides: Partial<OperationProjection> = {}): OperationProjection {
  return {
    id,
    name: id,
    type: 'test',
    state: 'running',
    progress: null,
    source: 'test',
    cancellable: false,
    ...overrides,
  }
}

beforeEach(() => {
  useOperationsStore.getState().clear()
})

describe('operationsStore', () => {
  it('starts empty with no fabricated jobs', () => {
    expect(useOperationsStore.getState().operations).toEqual([])
  })

  it('upserts a new operation from a backend projection', () => {
    useOperationsStore.getState().upsert(op('op:1'))
    expect(useOperationsStore.getState().operations).toHaveLength(1)
    expect(useOperationsStore.getState().operations[0]!.id).toBe('op:1')
  })

  it('updates an existing operation by id (progress)', () => {
    useOperationsStore.getState().upsert(op('op:1', { progress: 0 }))
    useOperationsStore.getState().upsert(op('op:1', { progress: 0.5, processed: 50n, total: 100n }))
    const stored = useOperationsStore.getState().operations[0]!
    expect(stored.progress).toBe(0.5)
    expect(stored.processed).toBe(50n)
    expect(stored.total).toBe(100n)
  })

  it('patches an existing operation partially', () => {
    useOperationsStore.getState().upsert(op('op:1', { progress: 0.2 }))
    useOperationsStore.getState().patch('op:1', { progress: 0.9, state: 'completed' })
    const stored = useOperationsStore.getState().operations[0]!
    expect(stored.progress).toBe(0.9)
    expect(stored.state).toBe('completed')
  })

  it('patch is a no-op for an unknown operation (never invents one)', () => {
    useOperationsStore.getState().patch('does-not-exist', { progress: 0.5 })
    expect(useOperationsStore.getState().operations).toEqual([])
  })

  it('removes an operation by id', () => {
    useOperationsStore.getState().upsert(op('op:1'))
    useOperationsStore.getState().remove('op:1')
    expect(useOperationsStore.getState().operations).toEqual([])
  })

  it('clears all operations', () => {
    useOperationsStore.getState().upsert(op('op:1'))
    useOperationsStore.getState().upsert(op('op:2'))
    useOperationsStore.getState().clear()
    expect(useOperationsStore.getState().operations).toEqual([])
  })

  it('marks cancellable=false by default (protocol does not support cancellation yet)', () => {
    useOperationsStore.getState().upsert(op('op:1'))
    expect(useOperationsStore.getState().operations[0]!.cancellable).toBe(false)
  })

  it('preserves cancellable=true when the backend projection declares it', () => {
    useOperationsStore.getState().upsert(op('op:1', { cancellable: true }))
    expect(useOperationsStore.getState().operations[0]!.cancellable).toBe(true)
  })
})

describe('operationsStore selectors', () => {
  it('activeOperations returns pending and running', () => {
    useOperationsStore.getState().upsert(op('a', { state: 'running' }))
    useOperationsStore.getState().upsert(op('b', { state: 'pending' }))
    useOperationsStore.getState().upsert(op('c', { state: 'completed' }))
    useOperationsStore.getState().upsert(op('d', { state: 'failed' }))
    const ops = useOperationsStore.getState().operations
    expect(activeOperations(ops).map((o) => o.id)).toEqual(['a', 'b'])
  })

  it('recentOperations returns completed/failed/cancelled', () => {
    useOperationsStore.getState().upsert(op('a', { state: 'running' }))
    useOperationsStore.getState().upsert(op('b', { state: 'completed' }))
    useOperationsStore.getState().upsert(op('c', { state: 'failed' }))
    useOperationsStore.getState().upsert(op('d', { state: 'cancelled' }))
    const ops = useOperationsStore.getState().operations
    expect(recentOperations(ops).map((o) => o.id)).toEqual(['b', 'c', 'd'])
  })
})

describe('operationsStore failed state', () => {
  it('stores the failure message from the backend projection', () => {
    useOperationsStore.getState().upsert(op('op:1', { state: 'failed', message: 'disk full' }))
    expect(useOperationsStore.getState().operations[0]!.message).toBe('disk full')
  })
})
