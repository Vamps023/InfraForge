export {}

declare global {
  interface Window {
    infraforgeDesktop?: Readonly<{
      platform: string
      versions: Readonly<{
        chrome: string
        electron: string
      }>
    }>
  }
}
