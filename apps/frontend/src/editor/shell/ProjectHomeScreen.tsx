import { FolderPlus, FolderOpen, Mountain, MapPin, Download, FileText } from 'lucide-react'
import type { CommandContext } from '../commands/useCommands'
import { executeCommand } from '../commands/useCommands'
import { useProjectStore } from '../../features/project/projectStore'

// ProjectHomeScreen — modern start screen shown when no project is open.
// Provides quick access to create/open projects and a getting-started
// guide for the terrain workflow. Does not add dashboards or fake data.
export function ProjectHomeScreen({ context }: { context: CommandContext }) {
  const lastError = useProjectStore((state) => state.lastError)

  return (
    <div className="home-screen" role="region" aria-label="Start screen">
      <div className="home-screen-content">
        <div className="home-logo">IF</div>
        <h1 className="home-title">InfraForge</h1>
        <p className="home-subtitle">
          Native infrastructure authoring, geospatial editing, and simulation preparation.
        </p>

        <div className="home-actions">
          <div className="home-action-row">
            <button
              type="button"
              className="button primary"
              onClick={() => void executeCommand('project.new', context)}
            >
              <FolderPlus size={16} /> New Project
            </button>
            <button
              type="button"
              className="button"
              onClick={() => void executeCommand('project.open', context)}
            >
              <FolderOpen size={16} /> Open Project
            </button>
          </div>
          {lastError ? (
            <div className="form-error" role="alert">
              <p>{lastError.message}</p>
            </div>
          ) : null}
        </div>

        <div className="home-section">
          <div className="home-section-title">Getting Started</div>
          <div className="home-getting-started">
            <div className="home-getting-started-item">
              <FolderPlus size={16} />
              <span>Create a new project and set its coordinate reference system.</span>
            </div>
            <div className="home-getting-started-item">
              <Mountain size={16} />
              <span>Switch to the Terrain workspace to import or download elevation data.</span>
            </div>
            <div className="home-getting-started-item">
              <Download size={16} />
              <span>Use Download Area to fetch DEM tiles for any location on Earth.</span>
            </div>
            <div className="home-getting-started-item">
              <MapPin size={16} />
              <span>Configure georeferencing to establish the canonical project origin.</span>
            </div>
            <div className="home-getting-started-item">
              <FileText size={16} />
              <span>Press Ctrl+Shift+P to open the command palette for quick access to all actions.</span>
            </div>
          </div>
        </div>

        <div className="home-version">InfraForge v0.1.0 — Developer Preview</div>
      </div>
    </div>
  )
}
