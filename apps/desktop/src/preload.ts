import { contextBridge, ipcRenderer } from 'electron'

const desktopApi = Object.freeze({
  platform: process.platform,
  versions: Object.freeze({
    chrome: process.versions.chrome,
    electron: process.versions.electron,
  }),
  getEngineBootstrap: () => ipcRenderer.invoke('engine:get-bootstrap') as Promise<unknown>,
})

contextBridge.exposeInMainWorld('infraforgeDesktop', desktopApi)
