import { contextBridge, ipcRenderer } from 'electron'

export interface PickDirectoryOptions {
  title: string
  buttonLabel?: string
}

export interface ViewportStatusPayload {
  state: 'unavailable' | 'starting' | 'ready' | 'suspended' | 'recreating' | 'device_lost' | 'failed' | 'stopped'
  detail: string
  validation?: boolean
  gpu?: string
  vulkan?: string
}

const desktopApi = Object.freeze({
  platform: process.platform,
  versions: Object.freeze({
    chrome: process.versions.chrome,
    electron: process.versions.electron,
  }),
  getEngineBootstrap: () => ipcRenderer.invoke('engine:get-bootstrap') as Promise<unknown>,
  pickDirectory: (options: PickDirectoryOptions) =>
    ipcRenderer.invoke('dialog:pick-directory', {
      title: String(options.title),
      buttonLabel: options.buttonLabel === undefined ? undefined : String(options.buttonLabel),
    }) as Promise<string | null>,
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
})

contextBridge.exposeInMainWorld('infraforgeDesktop', desktopApi)
