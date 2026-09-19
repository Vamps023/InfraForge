import { useState } from 'react'
import {
  ArrowLeft,
  Maximize2,
  PanelRightClose,
  PanelRightOpen,
  Search,
} from 'lucide-react'
import { useProjectStore } from '../../features/project/projectStore'
import { useRoadStore } from '../../features/road/roadStore'
import { useViewportStore } from '../../features/viewport/viewportStore'
import { useLayoutStore } from '../layout/layoutStore'
import { useShellUiStore } from './shellUiStore'
import { StatusDot, type StatusDotTone } from '../../ui/StatusDot'
import type { CommandContext } from '../commands/useCommands'
import { AppMenu } from './AppMenu'

export interface AppHeaderProps {
  context?: CommandContext
  activePage: 'projects' | 'terrain' | 'design'
  onNavigateProjects: () => void
  terrainViewMode?: 'map' | '3d'
  onTerrainViewModeChange?: (mode: 'map' | '3d') => void
}

export function AppHeader({
  context,
  activePage,
  onNavigateProjects,
  terrainViewMode,
  onTerrainViewModeChange,
}: AppHeaderProps) {
  const summary = useProjectStore((state) => state.summary)
  const roads = useRoadStore((state) => state.roads)
  const junctions = useRoadStore((state) => state.junctions)
  const openDialogCommand = useShellUiStore((state) => state.openDialogCommand)

  const [cameraProjection, setCameraProjection] = useState<'2d' | '3d'>('3d')

  const rightPanelVisible = useLayoutStore((state) => state.panels.right.visible)
  const setPanelVisible = useLayoutStore((state) => state.setPanelVisible)

  const handleToggleProjection = (mode: '2d' | '3d') => {
    setCameraProjection(mode)
    const cameraAction = mode === '2d' ? 'top' : 'perspective'
    window.infraforgeDesktop?.setViewportCamera?.(cameraAction)
  }

  const handleFitView = () => {
    window.infraforgeDesktop?.setViewportCamera?.('frame-all')
  }

  const subtitle =
    activePage === 'terrain'
      ? 'Step 1 · Terrain'
      : activePage === 'design'
        ? `Step 3 · Design · ${roads.length} road${roads.length !== 1 ? 's' : ''} · ${junctions.length} junction${junctions.length !== 1 ? 's' : ''}`
        : 'Projects'

  return (
    <header className="ogs-app-header" aria-label="Main Application Header">
      {/* Back to Projects */}
      <button
        type="button"
        className="header-back-btn"
        onClick={onNavigateProjects}
        title="Return to Projects overview"
      >
        <ArrowLeft size={16} />
        <span>Projects</span>
      </button>

      <div className="header-vertical-divider" />

      {/* Project Brand and Name */}
      <div className="header-title-block">
        <span className="header-project-name">
          {summary ? summary.displayName : 'InfraForge'}
        </span>
        <span className="header-subtitle">{subtitle}</span>
      </div>

      {context && (
        <>
          <div className="header-vertical-divider" />
          <AppMenu context={context} />
        </>
      )}

      <div className="header-spacer" />

      {/* Right Controls */}
      <div className="header-actions-row">
        {/* Terrain Map / 3D toggle */}
        {activePage === 'terrain' && onTerrainViewModeChange && (
          <div className="segmented-toggle" role="group" aria-label="Terrain view mode">
            <button
              type="button"
              className={`toggle-segment${terrainViewMode === 'map' ? ' active' : ''}`}
              onClick={() => onTerrainViewModeChange('map')}
            >
              Map
            </button>
            <button
              type="button"
              className={`toggle-segment${terrainViewMode === '3d' ? ' active' : ''}`}
              onClick={() => onTerrainViewModeChange('3d')}
            >
              3D
            </button>
          </div>
        )}

        {/* Design 2D / 3D toggle */}
        {activePage === 'design' && (
          <>
            <div className="segmented-toggle" role="group" aria-label="Viewport projection">
              <button
                type="button"
                className={`toggle-segment${cameraProjection === '2d' ? ' active' : ''}`}
                onClick={() => handleToggleProjection('2d')}
                title="Top-down orthographic 2D view"
              >
                2D
              </button>
              <button
                type="button"
                className={`toggle-segment${cameraProjection === '3d' ? ' active' : ''}`}
                onClick={() => handleToggleProjection('3d')}
                title="Perspective 3D view"
              >
                3D
              </button>
            </div>

            <button
              type="button"
              className="header-action-btn"
              onClick={handleFitView}
              title="Fit Network to View (H)"
            >
              <Maximize2 size={14} />
              <span>Fit View</span>
            </button>

            <button
              type="button"
              className={`header-action-btn${rightPanelVisible ? ' active' : ''}`}
              onClick={() => setPanelVisible('right', !rightPanelVisible)}
              title={rightPanelVisible ? 'Hide Inspector panel' : 'Show Inspector panel'}
            >
              {rightPanelVisible ? <PanelRightClose size={14} /> : <PanelRightOpen size={14} />}
              <span>Inspector</span>
            </button>
          </>
        )}

        {/* Search / Command Palette */}
        <button
          type="button"
          className="header-search-btn"
          onClick={() => openDialogCommand('command-palette')}
          title="Search commands (Ctrl+Shift+P)"
        >
          <Search size={14} />
          <span className="search-text">Search…</span>
          <kbd className="header-kbd">Ctrl+Shift+P</kbd>
        </button>

        {/* Renderer status dot */}
        <HeaderStatusDot />
      </div>
    </header>
  )
}

function HeaderStatusDot() {
  const rendererStatus = useViewportStore((state) => state.status)
  const tone: StatusDotTone =
    rendererStatus.state === 'ready'
      ? 'success'
      : rendererStatus.state === 'failed' ||
          rendererStatus.state === 'stopped' ||
          rendererStatus.state === 'device_lost'
        ? 'danger'
        : rendererStatus.state === 'suspended' || rendererStatus.state === 'unavailable'
          ? 'warning'
          : 'muted'

  return (
    <div className="header-status-indicator" title={`Vulkan Viewport: ${rendererStatus.detail}`}>
      <StatusDot tone={tone} />
      <span className="status-label">Viewport</span>
    </div>
  )
}
