import type { ReactNode } from 'react'

export type NavigatorTabId = 'scene' | 'layers' | 'assets' | 'sources'

export interface NavigatorTabDefinition {
  id: NavigatorTabId
  label: string
  order?: number
  // Predicate indicating whether the tab has real backing capabilities
  applies?: () => boolean
  render: () => ReactNode
}

class NavigatorTabRegistry {
  private readonly tabs = new Map<NavigatorTabId, NavigatorTabDefinition>()
  private readonly listeners = new Set<() => void>()

  register = (tab: NavigatorTabDefinition): void => {
    this.tabs.set(tab.id, tab)
    this.notify()
  }

  unregister = (id: NavigatorTabId): void => {
    if (this.tabs.delete(id)) {
      this.notify()
    }
  }

  get = (id: NavigatorTabId): NavigatorTabDefinition | undefined => {
    return this.tabs.get(id)
  }

  getAll = (): NavigatorTabDefinition[] => {
    return Array.from(this.tabs.values()).sort(
      (a, b) => (a.order ?? 100) - (b.order ?? 100),
    )
  }

  getAvailable = (): NavigatorTabDefinition[] => {
    return this.getAll().filter((tab) => !tab.applies || tab.applies())
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

export const navigatorTabRegistry = new NavigatorTabRegistry()
