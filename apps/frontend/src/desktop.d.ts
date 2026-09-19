export {}

type EngineConnectionInfo = Readonly<{
  host: '127.0.0.1'
  port: number
  sessionToken: string
  protocolMajor: number
  protocolMinor: number
  engineVersion: string
}>

type EngineBootstrap =
  | Readonly<{ state: 'ready'; connection: EngineConnectionInfo }>
  | Readonly<{ state: 'unavailable' | 'failed'; message: string }>

type ViewportStatusPayload = Readonly<{
  state: 'unavailable' | 'starting' | 'ready' | 'suspended' | 'recreating' | 'device_lost' | 'failed' | 'stopped'
  detail: string
  validation?: boolean
  gpu?: string
  vulkan?: string
}>

type ViewportRect = Readonly<{ x: number; y: number; width: number; height: number }>

type FileFilter = Readonly<{ name: string; extensions: ReadonlyArray<string> }>

declare global {
  interface Window {
    infraforgeDesktop?: Readonly<{
      platform: string
      versions: Readonly<{
        chrome: string
        electron: string
      }>
      getEngineBootstrap: () => Promise<EngineBootstrap>
      getRuntimeConfig: () => Promise<{
        geocoder: {
          endpoint: string
          minIntervalMs: number
          maxCacheEntries: number
          attribution: string
          userAgent: string
        }
        mapTiles: {
          provider: string
          url: string
          attribution: string
          maxZoom: number
        }
        diagnostics: {
          configSource: 'default' | 'config' | 'env'
          buildMarker: string
        }
      }>
      searchLocation: (query: string) => Promise<
        Array<{
          displayName: string
          lat: number
          lon: number
          boundingBox?: { south: number; north: number; west: number; east: number }
        }>
      >
      pickDirectory: (options: Readonly<{ title: string; buttonLabel?: string }>) => Promise<string | null>
      pickFile: (options: Readonly<{ title: string; filters?: ReadonlyArray<FileFilter> }>) => Promise<string | null>
      setViewportScene: (scene: Record<string, unknown>) => void
      setRoadPreview?: (points: Array<{ easting: number; northing: number }>) => void
      setViewportCamera: (action: 'focus-terrain' | 'frame-all' | 'perspective' | 'top', datasetUuid?: string) => void
      setViewportBounds: (rect: ViewportRect, dpiScale: number) => void
      setViewportVisible: (visible: boolean) => void
      onViewportStatus: (listener: (status: ViewportStatusPayload) => void) => () => void
      onViewportInteraction: (listener: (interaction: Readonly<{
        kind: 'primary-click' | 'pointer-move' | 'pointer-leave'; easting: number; northing: number; height: number; roadId?: string
      }>) => void) => () => void
      onMapTileDiagnostic: (listener: (diagnostic: Readonly<{
        state: 'started' | 'completed' | 'failed'
        url: string
        statusCode?: number
        error?: string
        method?: string
        resourceType?: string
      }>) => void) => () => void
      getDiagnostics: () => Promise<Readonly<{
        appVersion: string
        buildSha: string
        electronVersion: string
        chromeVersion: string
        nodeVersion: string
        platform: string
        arch: string
        isPackaged: boolean
        enginePath: string | null
        viewportPath: string | null
        projDataPath: string | null
        resourcesPath: string | null
        logPath: string
        userDataPath: string
      }>>
      openLogs: () => Promise<boolean>
      onDiagnosticsRequest: (listener: () => void) => () => void
    }>
  }
}
