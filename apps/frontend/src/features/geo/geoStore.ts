import { create } from 'zustand'
import type { GeoreferenceInfo } from '@infraforge/protocol'

// Frontend projection of the canonical project georeference plus resolved
// engine metadata. This mirrors backend truth — it never substitutes for it.
interface GeoState {
  info: GeoreferenceInfo | null
  revision: bigint | null
  applying: boolean
  lastError: { code: string; message: string } | null
  setInfo(info: GeoreferenceInfo, revision: bigint): void
  setApplying(value: boolean): void
  setLastError(error: { code: string; message: string } | null): void
  reset(): void
}

export const useGeoStore = create<GeoState>((set) => ({
  info: null,
  revision: null,
  applying: false,
  lastError: null,
  setInfo: (info, revision) => set({ info, revision }),
  setApplying: (value) => set({ applying: value }),
  setLastError: (error) => set({ lastError: error }),
  reset: () => set({ info: null, revision: null, applying: false, lastError: null }),
}))
