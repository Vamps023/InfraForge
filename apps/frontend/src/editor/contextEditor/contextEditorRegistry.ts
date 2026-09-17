import type { ReactNode } from 'react'

export interface ContextEditorContext {
  activeWorkspace: string
  selectedIds: string[]
  primaryId: string | null
}

export interface ContextEditorDefinition {
  id: string
  label: string
  // Predicate determining when this context editor applies to the active tool or selection
  applies: (context: ContextEditorContext) => boolean
  render: (context: ContextEditorContext) => ReactNode
}

class ContextEditorRegistry {
  private readonly editors = new Map<string, ContextEditorDefinition>()
  private readonly listeners = new Set<() => void>()

  register = (editor: ContextEditorDefinition): void => {
    this.editors.set(editor.id, editor)
    this.notify()
  }

  unregister = (id: string): void => {
    if (this.editors.delete(id)) {
      this.notify()
    }
  }

  get = (id: string): ContextEditorDefinition | undefined => {
    return this.editors.get(id)
  }

  getAll = (): ContextEditorDefinition[] => {
    return Array.from(this.editors.values())
  }

  resolve = (context: ContextEditorContext): ContextEditorDefinition | undefined => {
    for (const editor of this.editors.values()) {
      if (editor.applies(context)) {
        return editor
      }
    }
    return undefined
  }

  subscribe = (listener: () => void): () => void => {
    this.listeners.add(listener)
    return () => {
      this.listeners.delete(listener)
    }
  }

  private notify(): void {
    for (const listener of this.listeners) {
      listener()
    }
  }
}

export const contextEditorRegistry = new ContextEditorRegistry()
