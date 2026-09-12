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

declare global {
  interface Window {
    infraforgeDesktop?: Readonly<{
      platform: string
      versions: Readonly<{
        chrome: string
        electron: string
      }>
      getEngineBootstrap: () => Promise<EngineBootstrap>
      pickDirectory: (options: Readonly<{ title: string; buttonLabel?: string }>) => Promise<string | null>
      setViewportBounds: (rect: ViewportRect, dpiScale: number) => void
      setViewportVisible: (visible: boolean) => void
      onViewportStatus: (listener: (status: ViewportStatusPayload) => void) => () => void
    }>
  }
}
