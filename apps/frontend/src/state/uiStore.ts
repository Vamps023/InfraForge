import { create } from 'zustand'
import type { EngineSessionStatus } from '../lib/engineSession'

type BottomTab = 'Problems' | 'Operations'

interface UiState {
  activeBottomTab: BottomTab
  engineStatus: EngineSessionStatus
  setActiveBottomTab: (tab: BottomTab) => void
  setEngineStatus: (status: EngineSessionStatus) => void
}

export const useUiStore = create<UiState>((set) => ({
  activeBottomTab: 'Problems',
  engineStatus: { state: 'starting', message: 'Initializing desktop engine session…' },
  setActiveBottomTab: (activeBottomTab) => set({ activeBottomTab }),
  setEngineStatus: (engineStatus) => set({ engineStatus }),
}))
