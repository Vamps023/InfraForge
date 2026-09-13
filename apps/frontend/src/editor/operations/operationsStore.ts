import { create } from 'zustand'

// Operations store — a real framework for long-running backend work (terrain
// import, large data import, mesh generation, rebuild). State is driven by
// server-projected operation/job events, never fabricated. No fake running
// jobs are created; the store starts empty and only gains entries when the
// backend projects operation events.
//
// Cancellation is exposed in the model only when the backend actually
// supports it. The current protocol does not define a cancellation command,
// so cancellable is false for all operations until the protocol adds one.

export type OperationId = string
export type OperationState = 'pending' | 'running' | 'completed' | 'failed' | 'cancelled'

export interface OperationProjection {
  id: OperationId
  name: string
  type: string
  state: OperationState
  // Progress in [0,1] when the backend provides it; null when not provided.
  progress: number | null
  // Processed work units when available. Preserved as bigint to avoid
  // precision loss from uint64 protocol values.
  processed?: bigint
  // Total work units when available. Preserved as bigint to avoid
  // precision loss from uint64 protocol values.
  total?: bigint
  // Diagnostics/error message when state === 'failed'.
  message?: string
  // Source/domain that owns the operation, e.g. 'terrain', 'import'.
  source: string
  // Whether the backend supports cancelling this operation. False until the
  // protocol defines a cancellation command.
  cancellable: boolean
  // Canonical object ID the operation targets, when applicable.
  targetId?: string
}

interface OperationsState {
  operations: OperationProjection[]
  upsert: (operation: OperationProjection) => void
  remove: (id: OperationId) => void
  clear: () => void
  // Applies a partial update to an operation by ID. No-op if the operation
  // is not present (the store never invents operations).
  patch: (id: OperationId, patch: Partial<OperationProjection>) => void
}

function upsertOperation(list: OperationProjection[], operation: OperationProjection): OperationProjection[] {
  const index = list.findIndex((existing) => existing.id === operation.id)
  if (index === -1) {
    return [...list, operation]
  }
  const next = list.slice()
  next[index] = { ...next[index]!, ...operation }
  return next
}

export const useOperationsStore = create<OperationsState>((set, get) => ({
  operations: [],
  upsert: (operation) => set({ operations: upsertOperation(get().operations, operation) }),
  remove: (id) => set({ operations: get().operations.filter((op) => op.id !== id) }),
  clear: () => set({ operations: [] }),
  patch: (id, patch) =>
    set({
      operations: get().operations.map((op) =>
        op.id === id ? { ...op, ...patch } : op,
      ),
    }),
}))

// Selectors for the bottom panel.
export function activeOperations(operations: OperationProjection[]): OperationProjection[] {
  return operations.filter((op) => op.state === 'pending' || op.state === 'running')
}

export function recentOperations(operations: OperationProjection[]): OperationProjection[] {
  return operations.filter((op) => op.state === 'completed' || op.state === 'failed' || op.state === 'cancelled')
}
