import { contextBridge, ipcRenderer } from 'electron'

export interface PickDirectoryOptions {
  title: string
  buttonLabel?: string
}

export interface PickFileOptions {
  title: string
  filters?: Array<{ name: string; extensions: string[] }>
}

export interface ViewportStatusPayload {
  state: 'unavailable' | 'starting' | 'ready' | 'suspended' | 'recreating' | 'device_lost' | 'failed' | 'stopped'
  detail: string
  validation?: boolean
  gpu?: string
  vulkan?: string
}
export interface ViewportInteractionPayload {
  kind: 'primary-click' | 'pointer-move' | 'pointer-leave'; easting: number; northing: number; height: number; roadId?: string
}

const desktopApi = Object.freeze({
  platform: process.platform,
  versions: Object.freeze({
    chrome: process.versions.chrome,
    electron: process.versions.electron,
  }),
  getEngineBootstrap: () => ipcRenderer.invoke('engine:get-bootstrap') as Promise<unknown>,
  getRuntimeConfig: () => ipcRenderer.invoke('app:get-runtime-config') as Promise<unknown>,
  searchLocation: (query: string) => ipcRenderer.invoke('geocoder:search', String(query)) as Promise<unknown>,
  pickDirectory: (options: PickDirectoryOptions) =>
    ipcRenderer.invoke('dialog:pick-directory', {
      title: String(options.title),
      buttonLabel: options.buttonLabel === undefined ? undefined : String(options.buttonLabel),
    }) as Promise<string | null>,
  pickFile: (options: PickFileOptions) =>
    ipcRenderer.invoke('dialog:pick-file', {
      title: String(options.title),
      filters: Array.isArray(options.filters)
        ? options.filters.map((filter) => ({
            name: String(filter.name),
            extensions: filter.extensions.map((value) => String(value)),
          }))
        : [],
    }) as Promise<string | null>,
  setViewportScene: (scene: Record<string, unknown>) => {
    ipcRenderer.send('viewport:scene', scene)
  },
  setRoadPreview: (points: Array<{ easting: number; northing: number }>) => {
    ipcRenderer.send('viewport:road-preview', points)
  },
  setViewportBounds: (rect: { x: number; y: number; width: number; height: number }, dpiScale: number) => {
    ipcRenderer.send('viewport:set-bounds', { rect, dpiScale })
  },
  setViewportVisible: (visible: boolean) => {
    ipcRenderer.send('viewport:set-visible', visible)
  },
  onViewportStatus: (listener: (status: ViewportStatusPayload) => void) => {
    const channelListener = (_event: unknown, status: ViewportStatusPayload) => listener(status)
    ipcRenderer.on('viewport:status', channelListener)
    return () => {
      ipcRenderer.removeListener('viewport:status', channelListener)
    }
  },
  onViewportInteraction: (listener: (interaction: ViewportInteractionPayload) => void) => {
    const channelListener = (_event: unknown, interaction: ViewportInteractionPayload) => listener(interaction)
    ipcRenderer.on('viewport:interaction', channelListener)
    return () => ipcRenderer.removeListener('viewport:interaction', channelListener)
  },
  setViewportCamera: (action: 'focus-terrain' | 'frame-all' | 'perspective' | 'top', datasetUuid?: string) => {
    ipcRenderer.send('viewport:camera', action, datasetUuid)
  },
  onMapTileDiagnostic: (listener: (diagnostic: unknown) => void) => {
    const channelListener = (_event: unknown, diagnostic: unknown) => listener(diagnostic)
    ipcRenderer.on('map-tile:diagnostic', channelListener)
    return () => ipcRenderer.removeListener('map-tile:diagnostic', channelListener)
  },
  getDiagnostics: () => ipcRenderer.invoke('app:get-diagnostics') as Promise<unknown>,
  openLogs: () => ipcRenderer.invoke('app:open-logs') as Promise<boolean>,
  onDiagnosticsRequest: (listener: () => void) => {
    const channelListener = () => listener()
    ipcRenderer.on('menu:diagnostics', channelListener)
    return () => ipcRenderer.removeListener('menu:diagnostics', channelListener)
  },
})

contextBridge.exposeInMainWorld('infraforgeDesktop', desktopApi)
