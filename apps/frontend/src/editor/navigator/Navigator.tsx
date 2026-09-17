import { useEffect, useState } from 'react'
import { Outliner } from '../outliner/Outliner'
import { SourcesNavigator } from './SourcesNavigator'
import {
  navigatorTabRegistry,
  type NavigatorTabId,
} from './navigatorTabRegistry'
import { useWorkspaceStore } from '../shell/workspaceStore'
import { workspaceRegistry } from '../workspaces/workspaceRegistry'

// Register canonical built-in tabs on initial load
navigatorTabRegistry.register({
  id: 'scene',
  label: 'Scene',
  order: 10,
  render: () => <Outliner />,
})

navigatorTabRegistry.register({
  id: 'sources',
  label: 'Sources',
  order: 40,
  render: () => <SourcesNavigator />,
})

export interface NavigatorProps {
  initialTab?: NavigatorTabId
}

// Navigator — left dock container hosting capability tabs (Scene, Sources,
// and extensible for Layers, Assets when real models exist).
// Complies with docs/06_UI_UX/WORKSPACE_MODEL.md and APP_SHELL.md.
export function Navigator({ initialTab }: NavigatorProps) {
  const activeWorkspace = useWorkspaceStore((state) => state.activeWorkspace)
  const def = workspaceRegistry.get(activeWorkspace)

  // Default to workspace's recommended tab, or 'scene'
  const defaultTab = def?.defaultNavigatorTab ?? initialTab ?? 'scene'
  const [activeTab, setActiveTab] = useState<NavigatorTabId>(defaultTab)

  // Synchronize with workspace recommendation when workspace changes
  useEffect(() => {
    if (def?.defaultNavigatorTab) {
      setActiveTab(def.defaultNavigatorTab)
    }
  }, [activeWorkspace, def?.defaultNavigatorTab])

  const tabs = navigatorTabRegistry.getAvailable()
  const activeTabDef = navigatorTabRegistry.get(activeTab) ?? tabs[0]

  return (
    <div className="navigator-host" aria-label="Project navigator">
      <div
        className="navigator-tabs"
        role="tablist"
        aria-label="Navigator capability tabs"
      >
        {tabs.map((tab) => {
          const isSelected = tab.id === activeTabDef?.id
          return (
            <button
              key={tab.id}
              role="tab"
              type="button"
              className={`navigator-tab${isSelected ? ' active' : ''}`}
              aria-selected={isSelected}
              aria-controls={`navigator-tabpanel-${tab.id}`}
              id={`navigator-tab-${tab.id}`}
              onClick={() => setActiveTab(tab.id)}
            >
              {tab.label}
            </button>
          )
        })}
      </div>

      <div
        id={`navigator-tabpanel-${activeTabDef?.id}`}
        role="tabpanel"
        aria-labelledby={`navigator-tab-${activeTabDef?.id}`}
        className="navigator-panel-body"
      >
        {activeTabDef ? activeTabDef.render() : null}
      </div>
    </div>
  )
}
