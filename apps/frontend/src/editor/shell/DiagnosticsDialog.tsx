import { useEffect, useState } from 'react'

interface DiagnosticsInfo {
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
}

interface DiagnosticsDialogProps {
  onClose: () => void
}

// v0.1 diagnostics surface: displays build identity, resolved native
// executable paths, PROJ data path, and runtime versions so support sessions
// can identify the exact environment without a terminal. Triggered from the
// Help menu or command palette.
export function DiagnosticsDialog({ onClose }: DiagnosticsDialogProps) {
  const [info, setInfo] = useState<DiagnosticsInfo | null>(null)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    const desktop = window.infraforgeDesktop
    if (!desktop?.getDiagnostics) {
      setError('Diagnostics are only available in the desktop application.')
      return
    }
    let cancelled = false
    void (async () => {
      try {
        const result = await desktop.getDiagnostics() as DiagnosticsInfo
        if (!cancelled) {
          setInfo(result)
        }
      } catch (err) {
        if (!cancelled) {
          setError(err instanceof Error ? err.message : String(err))
        }
      }
    })()
    return () => {
      cancelled = true
    }
  }, [])

  const handleOpenLogs = () => {
    void window.infraforgeDesktop?.openLogs?.()
  }

  const rows: Array<[string, string]> = info ? [
    ['InfraForge', info.appVersion],
    ['Build SHA', info.buildSha],
    ['Electron', info.electronVersion],
    ['Chromium', info.chromeVersion],
    ['Node.js', info.nodeVersion],
    ['Platform', `${info.platform} (${info.arch})`],
    ['Packaged', info.isPackaged ? 'yes' : 'no'],
    ['Engine', info.enginePath ?? 'not found'],
    ['Viewport', info.viewportPath ?? 'not found'],
    ['PROJ data', info.projDataPath ?? 'not found'],
    ['Resources', info.resourcesPath ?? 'n/a'],
    ['User data', info.userDataPath],
    ['Logs', info.logPath],
  ] : []

  return (
    <div className="dialog-overlay" onClick={onClose}>
      <div className="dialog" style={{ width: '560px' }} onClick={(e) => e.stopPropagation()}>
        <h2>Diagnostics</h2>
        {error ? (
          <p className="form-error">{error}</p>
        ) : !info ? (
          <p className="form-static">Loading…</p>
        ) : (
          <div style={{ display: 'flex', flexDirection: 'column', gap: '4px', marginBottom: '12px' }}>
            {rows.map(([label, value]) => (
              <div key={label} style={{ display: 'flex', gap: '8px', fontSize: '12px' }}>
                <span className="form-label" style={{ width: '90px', flexShrink: 0 }}>{label}</span>
                <span style={{ color: 'var(--text)', wordBreak: 'break-all' }}>{value}</span>
              </div>
            ))}
          </div>
        )}
        <div className="dialog-actions">
          <button className="btn" onClick={handleOpenLogs} disabled={!info}>
            Open Logs Folder
          </button>
          <button className="btn btn-primary" onClick={onClose}>
            Close
          </button>
        </div>
      </div>
    </div>
  )
}
