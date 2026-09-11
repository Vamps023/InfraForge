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

declare global {
  interface Window {
    infraforgeDesktop?: Readonly<{
      platform: string
      versions: Readonly<{
        chrome: string
        electron: string
      }>
      getEngineBootstrap: () => Promise<EngineBootstrap>
    }>
  }
}
