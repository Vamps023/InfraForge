import { create } from 'zustand'

type BottomTab = 'Problems' | 'Operations'

interface UiState {
  activeBottomTab: BottomTab
  setActiveBottomTab: (tab: BottomTab) => void
}

export const useUiStore = create<UiState>((set) => ({
  activeBottomTab: 'Problems',
  setActiveBottomTab: (activeBottomTab) => set({ activeBottomTab }),
}))
