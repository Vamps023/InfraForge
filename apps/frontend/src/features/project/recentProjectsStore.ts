import { create } from 'zustand'
import type { ProjectSummary, TrafficSide } from '@infraforge/protocol'

export interface RecentProject {
  id: string
  name: string
  directory: string
  createdAt: string
  modifiedAt?: string
  horizontalCrs?: string
  trafficSide?: 'LEFT' | 'RIGHT'
  roadCount?: number
  hasTerrain?: boolean
}

const STORAGE_KEY = 'infraforge:recent-projects'

function loadFromStorage(): RecentProject[] {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (!raw) return []
    const parsed = JSON.parse(raw)
    if (Array.isArray(parsed)) {
      return parsed
    }
  } catch {
    // Ignore storage parse errors
  }
  return []
}

function saveToStorage(projects: RecentProject[]) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(projects))
  } catch {
    // Ignore storage write errors
  }
}

interface RecentProjectsState {
  projects: RecentProject[]
  addOrUpdate: (project: RecentProject) => void
  recordFromSummary: (summary: ProjectSummary, roadCount?: number, hasTerrain?: boolean) => void
  remove: (directory: string) => void
}

export const useRecentProjectsStore = create<RecentProjectsState>((set) => ({
  projects: loadFromStorage(),

  addOrUpdate: (project) => {
    set((state) => {
      const filtered = state.projects.filter((p) => p.directory !== project.directory)
      const next = [project, ...filtered]
      saveToStorage(next)
      return { projects: next }
    })
  },

  recordFromSummary: (summary, roadCount = 0, hasTerrain = false) => {
    set((state) => {
      const existing = state.projects.find((p) => p.directory === summary.directory)
      const trafficSideStr: 'LEFT' | 'RIGHT' =
        summary.trafficSide === 1 ? 'LEFT' : 'RIGHT'

      const updated: RecentProject = {
        id: summary.projectUuid || summary.directory,
        name: summary.displayName,
        directory: summary.directory,
        createdAt: summary.createdAt || existing?.createdAt || new Date().toISOString(),
        modifiedAt: summary.modifiedAt || new Date().toISOString(),
        horizontalCrs: summary.georeference?.horizontalCrs || existing?.horizontalCrs || 'EPSG:4978',
        trafficSide: trafficSideStr,
        roadCount: roadCount !== undefined ? roadCount : existing?.roadCount ?? 0,
        hasTerrain: hasTerrain !== undefined ? hasTerrain : existing?.hasTerrain ?? false,
      }

      const filtered = state.projects.filter((p) => p.directory !== summary.directory)
      const next = [updated, ...filtered]
      saveToStorage(next)
      return { projects: next }
    })
  },

  remove: (directory) => {
    set((state) => {
      const next = state.projects.filter((p) => p.directory !== directory)
      saveToStorage(next)
      return { projects: next }
    })
  },
}))
