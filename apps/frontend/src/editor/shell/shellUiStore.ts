import { create } from 'zustand'

// Shell-level UI state that builtin commands mutate: which blocking overlay
// dialog is open. This is presentation state only (ADR-0009); it is not
// project/domain state. Commands toggle these instead of components holding
// local useState so the command registry remains the single entry point for
// user actions.

export type ShellDialog = 'new-project' | 'georeference' | 'command-palette' | 'import-terrain' | null

interface ShellUiState {
  openDialog: ShellDialog
  openDialogCommand: (dialog: ShellDialog) => void
  closeDialog: () => void
}

export const useShellUiStore = create<ShellUiState>((set) => ({
  openDialog: null,
  openDialogCommand: (openDialog) => set({ openDialog }),
  closeDialog: () => set({ openDialog: null }),
}))
