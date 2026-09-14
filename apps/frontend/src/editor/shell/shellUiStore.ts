import { create } from 'zustand'

// Shell-level UI state that builtin commands mutate: which blocking overlay
// dialog is open. This is presentation state only (ADR-0009); it is not
// project/domain state. Commands toggle these instead of components holding
// local useState so the command registry remains the single entry point for
// user actions.

export type ShellDialog = 'new-project' | 'georeference' | 'command-palette' | 'import-terrain' | 'diagnostics' | null

// When the import-terrain dialog is open, this controls which source tab
// is initially active ('local-file' or 'download-area'). Set before opening
// the dialog so the ContextToolbar can deep-link to a specific mode.
export type TerrainImportMode = 'local-file' | 'download-area'

interface ShellUiState {
  openDialog: ShellDialog
  terrainImportMode: TerrainImportMode
  openDialogCommand: (dialog: ShellDialog) => void
  openTerrainImport: (mode: TerrainImportMode) => void
  closeDialog: () => void
}

export const useShellUiStore = create<ShellUiState>((set) => ({
  openDialog: null,
  terrainImportMode: 'local-file',
  openDialogCommand: (openDialog) => set({ openDialog }),
  openTerrainImport: (terrainImportMode) => set({ openDialog: 'import-terrain', terrainImportMode }),
  closeDialog: () => set({ openDialog: null }),
}))
