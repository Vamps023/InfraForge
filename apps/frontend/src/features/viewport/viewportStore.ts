import { create } from 'zustand'

// UI projection of the native viewport process status. The renderer process
// itself is owned by the viewport module; this store only mirrors the status
// records the shell forwards.
export type ViewportState =
  | 'unavailable'
  | 'starting'
  | 'ready'
  | 'suspended'
  | 'recreating'
  | 'device_lost'
  | 'failed'
  | 'stopped'

export interface ViewportStatusProjection {
  state: ViewportState
  detail: string
  validation?: boolean
  gpu?: string
  vulkan?: string
}

interface ViewportUiState {
  status: ViewportStatusProjection
  setStatus: (status: ViewportStatusProjection) => void
}

export const useViewportStore = create<ViewportUiState>((set) => ({
  status: { state: 'unavailable', detail: 'Native viewport has not reported yet.' },
  setStatus: (status) => set({ status }),
}))

export function viewportSurfaceActive(state: ViewportState): boolean {
  return state === 'ready' || state === 'recreating'
}
