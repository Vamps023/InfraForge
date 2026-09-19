import { useState } from 'react'
import {
  FolderOpen,
  FolderPlus,
  HardDrive,
  Layers,
  Map as MapIcon,
  Plus,
  Trash2,
  Mountain,
  Building2,
  Signal,
  Eye,
  Play,
  CheckCircle2,
} from 'lucide-react'
import type { EngineClient } from '../../lib/engineSession'
import { openProject, closeProject } from './projectApi'
import { useProjectStore } from './projectStore'
import { useRecentProjectsStore, type RecentProject } from './recentProjectsStore'

export interface ProjectsPageProps {
  client: EngineClient | null
  onOpenDesign: (directory: string) => void
  onOpenTerrain: (directory: string) => void
  onNewProject: () => void
}

const WORKFLOW_STEPS = [
  { step: '1', label: 'Terrain', icon: Mountain, desc: 'Choose project area, georeference CRS, and elevation DEM.', active: true },
  { step: '2', label: 'Buildings', icon: Building2, desc: 'Import 3D OpenStreetMap building context.', active: false },
  { step: '3', label: 'Design', icon: Layers, desc: 'Create roads, rails, lane sections, and junctions.', active: true },
  { step: '4', label: 'Infrastructure', icon: Signal, desc: 'Place signals, signs, rules, and dispatch.', active: false },
  { step: '5', label: 'Review', icon: Eye, desc: 'Inspect the complete 3D digital twin scene.', active: false },
  { step: '6', label: 'Simulate', icon: Play, desc: 'Run live traffic & train simulation.', active: false },
]

export function ProjectsPage({
  client,
  onOpenDesign,
  onOpenTerrain,
  onNewProject,
}: ProjectsPageProps) {
  const projects = useRecentProjectsStore((state) => state.projects)
  const removeRecent = useRecentProjectsStore((state) => state.remove)
  const currentSummary = useProjectStore((state) => state.summary)
  const [loadingDirectory, setLoadingDirectory] = useState<string | null>(null)
  const [deleteTarget, setDeleteTarget] = useState<RecentProject | null>(null)
  const [errorMessage, setErrorMessage] = useState<string | null>(null)

  const handleOpenViaPicker = async () => {
    const desktop = window.infraforgeDesktop
    if (!desktop?.pickDirectory) {
      setErrorMessage('Desktop directory picker not available.')
      return
    }
    const dir = await desktop.pickDirectory({
      title: 'Select an InfraForge (.iforge) project directory',
      buttonLabel: 'Open Project',
    })
    if (!dir) return
    await handleOpenProject(dir, 'design')
  }

  const handleOpenProject = async (directory: string, target: 'design' | 'terrain') => {
    if (!client) {
      setErrorMessage('Engine is not connected.')
      return
    }
    setLoadingDirectory(directory)
    setErrorMessage(null)
    try {
      if (currentSummary && currentSummary.directory !== directory) {
        await closeProject(client).catch(() => undefined)
      }
      await openProject(client, directory)
      if (target === 'terrain') {
        onOpenTerrain(directory)
      } else {
        onOpenDesign(directory)
      }
    } catch (err: unknown) {
      setErrorMessage(err instanceof Error ? err.message : 'Failed to open project.')
    } finally {
      setLoadingDirectory(null)
    }
  }

  const confirmDelete = async () => {
    if (!deleteTarget) return
    const targetDir = deleteTarget.directory
    if (currentSummary?.directory === targetDir && client) {
      await closeProject(client).catch(() => undefined)
    }
    removeRecent(targetDir)
    setDeleteTarget(null)
  }

  return (
    <main className="projects-page" role="main" aria-label="InfraForge Projects">
      {/* Ambient background glow */}
      <div className="projects-ambient-glow" aria-hidden="true" />

      <div className="projects-container">
        {/* Header */}
        <header className="projects-header">
          <div className="projects-brand-group">
            <div className="brand-mark">IF</div>
            <div>
              <p className="projects-brand-subtitle">INFRAFORGE</p>
              <h1 className="projects-title">Projects</h1>
            </div>
          </div>

          <div className="projects-header-actions">
            <button
              type="button"
              className="button"
              onClick={handleOpenViaPicker}
              title="Open an existing project folder from disk"
            >
              <FolderOpen size={16} />
              <span>Open Project…</span>
            </button>

            <button
              type="button"
              className="button primary"
              onClick={onNewProject}
              title="Create a new infrastructure project"
            >
              <Plus size={16} />
              <span>New Project</span>
            </button>
          </div>
        </header>

        {errorMessage && (
          <div className="projects-error-banner" role="alert">
            <span>{errorMessage}</span>
            <button type="button" onClick={() => setErrorMessage(null)}>Dismiss</button>
          </div>
        )}

        {/* Project Workflow Guide */}
        <section className="projects-workflow-section" aria-labelledby="workflow-heading">
          <div className="projects-workflow-header">
            <h2 id="workflow-heading" className="projects-section-heading">Project workflow</h2>
            <p className="projects-section-subheading">
              Follow these steps in order. You can return to any workspace at any time.
            </p>
          </div>

          <div className="projects-workflow-steps">
            {WORKFLOW_STEPS.map((step) => {
              const Icon = step.icon
              return (
                <div
                  key={step.step}
                  className={`workflow-step-card${step.active ? ' active' : ' disabled'}`}
                >
                  <div className="workflow-step-badge">
                    <span>{step.step}</span>
                  </div>
                  <div className="workflow-step-body">
                    <div className="workflow-step-title-row">
                      <Icon size={14} className="workflow-step-icon" />
                      <span className="workflow-step-label">{step.label}</span>
                    </div>
                    <p className="workflow-step-desc">{step.desc}</p>
                  </div>
                </div>
              )
            })}
          </div>
        </section>

        {/* Project Grid */}
        <section className="projects-grid-section">
          {projects.length === 0 ? (
            <div className="projects-empty-state">
              <div className="empty-state-icon-box">
                <FolderOpen size={28} />
              </div>
              <h2 className="empty-state-title">No projects yet</h2>
              <p className="empty-state-desc">
                Create your first project to start drawing roads and working with terrain.
              </p>
              <button
                type="button"
                className="button primary"
                onClick={onNewProject}
              >
                <Plus size={16} />
                <span>Create a project</span>
              </button>
            </div>
          ) : (
            <div className="projects-grid">
              {projects.map((proj) => {
                const isLoading = loadingDirectory === proj.directory
                const isCurrent = currentSummary?.directory === proj.directory
                const dateStr = proj.createdAt
                  ? new Date(proj.createdAt).toLocaleDateString()
                  : 'Recent'

                return (
                  <article
                    key={proj.directory}
                    className={`project-card${isCurrent ? ' current-project' : ''}`}
                    aria-label={`Project: ${proj.name}`}
                  >
                    <div className="project-card-header">
                      <div className="project-card-title-row">
                        <h3 className="project-card-title" title={proj.name}>
                          {proj.name}
                        </h3>
                        <button
                          type="button"
                          className="project-delete-btn"
                          title="Delete / remove project from list"
                          aria-label={`Remove project ${proj.name}`}
                          onClick={() => setDeleteTarget(proj)}
                        >
                          <Trash2 size={14} />
                        </button>
                      </div>

                      <p className="project-card-date">Created {dateStr}</p>

                      <div className="project-badges-row">
                        {proj.horizontalCrs && (
                          <span className="badge crs-badge">
                            {proj.horizontalCrs}
                          </span>
                        )}
                        <span className="badge traffic-badge">
                          {proj.trafficSide === 'LEFT' ? 'LHT' : 'RHT'}
                        </span>
                        {proj.roadCount !== undefined && proj.roadCount > 0 && (
                          <span className="project-stat">
                            · {proj.roadCount} road{proj.roadCount > 1 ? 's' : ''}
                          </span>
                        )}
                        {proj.hasTerrain && (
                          <span className="project-stat">· terrain</span>
                        )}
                      </div>
                    </div>

                    <div className="project-card-footer">
                      {isLoading ? (
                        <div className="project-loading-indicator">
                          <span className="spinner" />
                          <span>Loading project…</span>
                        </div>
                      ) : (
                        <button
                          type="button"
                          className="button primary full-width"
                          onClick={() => void handleOpenProject(proj.directory, 'design')}
                          title="Step 3: Open the road and rail design workspace"
                        >
                          <Layers size={14} />
                          <span>Step 3: Open Design Editor</span>
                        </button>
                      )}

                      <div className="project-quick-actions">
                        <button
                          type="button"
                          className="button outline compact"
                          disabled={isLoading}
                          onClick={() => void handleOpenProject(proj.directory, 'terrain')}
                          title="Step 1: define terrain and working area"
                        >
                          <span className="step-num">1</span>
                          <MapIcon size={13} />
                          <span>Terrain</span>
                        </button>

                        <button
                          type="button"
                          className="button outline compact"
                          disabled={isLoading}
                          onClick={() => void handleOpenProject(proj.directory, 'design')}
                          title="Step 3: road authoring and lane design"
                        >
                          <span className="step-num">3</span>
                          <Layers size={13} />
                          <span>Design</span>
                        </button>
                      </div>
                    </div>
                  </article>
                )
              })}
            </div>
          )}
        </section>
      </div>

      {/* Delete Confirmation Modal */}
      {deleteTarget && (
        <div className="dialog-overlay" role="presentation">
          <div
            className="dialog-box"
            role="dialog"
            aria-labelledby="delete-dialog-title"
            aria-modal="true"
          >
            <div className="dialog-header">
              <h2 id="delete-dialog-title">Remove Project</h2>
            </div>
            <div className="dialog-body">
              <p>
                Are you sure you want to remove <strong>{deleteTarget.name}</strong> from your recent projects?
              </p>
              <p className="dialog-help" style={{ marginTop: '8px' }}>
                Path: <code style={{ fontSize: '11px' }}>{deleteTarget.directory}</code>
              </p>
            </div>
            <div className="dialog-actions">
              <button
                type="button"
                className="button"
                onClick={() => setDeleteTarget(null)}
              >
                Cancel
              </button>
              <button
                type="button"
                className="button danger"
                onClick={() => void confirmDelete()}
              >
                Remove
              </button>
            </div>
          </div>
        </div>
      )}
    </main>
  )
}
