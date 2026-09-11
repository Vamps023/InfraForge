import { app, BrowserWindow, dialog, ipcMain } from 'electron'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { EngineSupervisor } from './EngineSupervisor.js'

const currentDirectory = path.dirname(fileURLToPath(import.meta.url))
let engineSupervisor: EngineSupervisor | null = null

function createMainWindow(): BrowserWindow {
  const preloadPath = path.join(currentDirectory, 'preload.js')

  const window = new BrowserWindow({
    width: 1600,
    height: 960,
    minWidth: 960,
    minHeight: 640,
    backgroundColor: '#0d1014',
    show: false,
    webPreferences: {
      preload: preloadPath,
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: true,
    },
  })

  window.removeMenu()
  window.webContents.setWindowOpenHandler(() => ({ action: 'deny' }))
  window.once('ready-to-show', () => window.show())

  const developmentUrl = process.env.INFRAFORGE_FRONTEND_URL
  if (developmentUrl) {
    const parsed = new URL(developmentUrl)
    if (parsed.hostname !== '127.0.0.1' && parsed.hostname !== 'localhost') {
      throw new Error('INFRAFORGE_FRONTEND_URL must target localhost during development')
    }
    void window.loadURL(parsed.toString())
  } else {
    const frontendPath = path.resolve(currentDirectory, '../../frontend/dist/index.html')
    void window.loadFile(frontendPath)
  }

  return window
}

app.whenReady().then(async () => {
  engineSupervisor = new EngineSupervisor()
  await engineSupervisor.start()

  ipcMain.handle('engine:get-bootstrap', () => engineSupervisor?.snapshot() ?? {
    state: 'failed',
    message: 'Engine supervisor is unavailable.',
  })

  // The shell's only file-system role: user-authorized OS dialogs. The
  // renderer never receives unrestricted Node or arbitrary IPC passthrough.
  ipcMain.handle(
    'dialog:pick-directory',
    async (event, options: { title?: unknown; buttonLabel?: unknown }) => {
      const dialogOptions = {
        title: typeof options?.title === 'string' ? options.title : 'Select a directory',
        buttonLabel: typeof options?.buttonLabel === 'string' ? options.buttonLabel : undefined,
        properties: ['openDirectory', 'createDirectory', 'dontAddToRecent'] as Array<'openDirectory' | 'createDirectory' | 'dontAddToRecent'>,
      }
      const ownerWindow = BrowserWindow.fromWebContents(event.sender)
      const result = ownerWindow
        ? await dialog.showOpenDialog(ownerWindow, dialogOptions)
        : await dialog.showOpenDialog(dialogOptions)
      if (result.canceled || result.filePaths.length !== 1) {
        return null
      }
      return result.filePaths[0] ?? null
    },
  )

  createMainWindow()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createMainWindow()
    }
  })
})

app.on('before-quit', () => {
  engineSupervisor?.stop()
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
