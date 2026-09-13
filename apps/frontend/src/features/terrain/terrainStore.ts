import { create } from 'zustand'
import type { JobRecord, TerrainDatasetInfo, TerrainProbeSourceResult } from '@infraforge/protocol'

// Frontend projection of terrain state. Everything here mirrors engine
// truth (command results and events); the frontend never owns canonical
// terrain data and never receives raster payloads.
interface TerrainState {
  datasets: TerrainDatasetInfo[]
  selectedDatasetUuid: string | null
  jobs: JobRecord[]
  probe: TerrainProbeSourceResult | null
  probeError: string | null
  importing: boolean
  lastError: string | null
  setDatasets(datasets: TerrainDatasetInfo[]): void
  selectDataset(uuid: string | null): void
  upsertDataset(dataset: TerrainDatasetInfo): void
  setJobs(jobs: JobRecord[]): void
  applyJob(record: JobRecord): void
  setProbe(probe: TerrainProbeSourceResult | null): void
  setProbeError(error: string | null): void
  setImporting(value: boolean): void
  setLastError(error: string | null): void
  reset(): void
}

function upsertJob(jobs: JobRecord[], record: JobRecord): JobRecord[] {
  const index = jobs.findIndex((job) => job.jobId === record.jobId)
  if (index === -1) {
    return [...jobs, record]
  }
  const next = [...jobs]
  next[index] = record
  return next
}

export const useTerrainStore = create<TerrainState>((set) => ({
  datasets: [],
  selectedDatasetUuid: null,
  jobs: [],
  probe: null,
  probeError: null,
  importing: false,
  lastError: null,
  setDatasets: (datasets) =>
    set((state) => ({
      datasets,
      selectedDatasetUuid: datasets.some((d) => d.datasetUuid === state.selectedDatasetUuid)
        ? state.selectedDatasetUuid
        : null,
    })),
  selectDataset: (uuid) => set({ selectedDatasetUuid: uuid }),
  upsertDataset: (dataset) =>
    set((state) => {
      const index = state.datasets.findIndex((d) => d.datasetUuid === dataset.datasetUuid)
      const datasets =
        index === -1
          ? [...state.datasets, dataset]
          : state.datasets.map((d, i) => (i === index ? dataset : d))
      return { datasets }
    }),
  setJobs: (jobs) => set({ jobs }),
  applyJob: (record) => set((state) => ({ jobs: upsertJob(state.jobs, record) })),
  setProbe: (probe) => set({ probe, probeError: null }),
  setProbeError: (probeError) => set({ probeError, probe: null }),
  setImporting: (importing) => set({ importing }),
  setLastError: (lastError) => set({ lastError }),
  reset: () =>
    set({
      datasets: [],
      selectedDatasetUuid: null,
      jobs: [],
      probe: null,
      probeError: null,
      importing: false,
      lastError: null,
    }),
}))
