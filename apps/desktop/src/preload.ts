import { contextBridge, ipcRenderer } from 'electron'

export interface PickDirectoryOptions {
  title: string
  buttonLabel?: string
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
})

contextBridge.exposeInMainWorld('infraforgeDesktop', desktopApi)
