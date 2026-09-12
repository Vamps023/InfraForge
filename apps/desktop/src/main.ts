import { app, BrowserWindow, dialog, ipcMain, screen } from 'electron'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { EngineSupervisor } from './EngineSupervisor.js'
import { ViewportSupervisor, type ViewportPlacement } from './ViewportSupervisor.js'

const currentDirectory = path.dirname(fileURLToPath(import.meta.url))
let engineSupervisor: EngineSupervisor | null = null
let viewportSupervisor: ViewportSupervisor | null = null

interface ViewportBoundsPayload {
  rect: { x: number; y: number; width: number; height: number }
  dpiScale: number
}

function isValidBoundsPayload(payload: unknown): payload is ViewportBoundsPayload {
  if (typeof payload !== 'object' || payload === null) {
    return false
  }
  const candidate = payload as ViewportBoundsPayload
  const rect = candidate.rect
  return (
    typeof candidate.dpiScale === 'number' &&
    candidate.dpiScale > 0 &&
    typeof rect === 'object' &&
    rect !== null &&
    Number.isFinite(rect.x) &&
    Number.isFinite(rect.y) &&
    Number.isFinite(rect.width) &&
    Number.isFinite(rect.height) &&
    rect.width >= 0 &&
    rect.height >= 0
  )
}

// Converts a CSS-pixel rect in the page into a physical-pixel screen
// placement. screen.dipToScreenRect performs the DIP→physical conversion
// relative to the display that actually hosts the rect, so mixed-DPI
// multi-monitor setups convert correctly — a single display's scaleFactor
// cannot, because global DIP coordinates span displays with different
// scales. The viewport process converts screen coordinates to
// parent-client coordinates at apply time, so window moves between send and
// apply never misplace the child surface.
function computePlacement(window: BrowserWindow, payload: ViewportBoundsPayload): ViewportPlacement {
  const contentBounds = window.getContentBounds()
  const screenRect = screen.dipToScreenRect(window, {
    x: contentBounds.x + payload.rect.x,
    y: contentBounds.y + payload.rect.y,
    width: payload.rect.width,
    height: payload.rect.height,
  })
  return {
    screenX: screenRect.x,
    screenY: screenRect.y,
    width: screenRect.width,
    height: screenRect.height,
    dpiScale: payload.dpiScale,
  }
}

function repositionViewport(window: BrowserWindow): void {
  if (viewportSupervisor?.snapshot().state !== 'ready') {
    return
  }
  const latest = lastViewportBounds.get(window)
  if (latest) {
    viewportSupervisor.place(computePlacement(window, latest))
  }
}

const lastViewportBounds = new WeakMap<BrowserWindow, ViewportBoundsPayload>()

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

  // Window-level geometry changes that the page's ResizeObserver cannot see
  // (pure moves, maximize, monitor switches with unchanged layout) must
  // re-place the child surface against fresh screen coordinates.
  const reposition = () => repositionViewport(window)
  window.on('move', reposition)
  window.on('maximize', reposition)
  window.on('restore', reposition)
  window.on('enter-full-screen', reposition)
  window.on('leave-full-screen', reposition)

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

  viewportSupervisor = new ViewportSupervisor()

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

  // Native viewport hosting: the renderer measures its viewport-host
  // rectangle; the shell converts it to a physical screen placement, owns
  // the native viewport process, and forwards renderer status back.
  ipcMain.on('viewport:set-bounds', (event, payload: unknown) => {
    const window = BrowserWindow.fromWebContents(event.sender)
    if (!window || !isValidBoundsPayload(payload)) {
      return
    }
    lastViewportBounds.set(window, payload)
    const placement = computePlacement(window, payload)

    if (viewportSupervisor === null) {
      return
    }
    if (viewportSupervisor.snapshot().state === 'unavailable' || viewportSupervisor.snapshot().state === 'stopped') {
      viewportSupervisor.setStatusListener((status) => {
        if (!window.isDestroyed()) {
          window.webContents.send('viewport:status', status)
        }
      })
      void viewportSupervisor.start(window.getNativeWindowHandle(), placement)
      return
    }
    viewportSupervisor.place(placement)
  })

  ipcMain.on('viewport:set-visible', (_event, visible: unknown) => {
    if (typeof visible === 'boolean') {
      viewportSupervisor?.setVisible(visible)
    }
  })

  createMainWindow()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createMainWindow()
    }
  })
})

app.on('before-quit', () => {
  viewportSupervisor?.stop()
  engineSupervisor?.stop()
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
