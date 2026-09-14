import { stat } from 'node:fs/promises'
import path from 'node:path'

export type NativeExecutableKind = 'engine' | 'viewport'

async function existingFile(candidate: string): Promise<string | null> {
  try {
    return (await stat(candidate)).isFile() ? candidate : null
  } catch {
    return null
  }
}

export async function resolveNativeExecutable(kind: NativeExecutableKind): Promise<string | null> {
  const envName = kind === 'engine' ? 'INFRAFORGE_ENGINE_PATH' : 'INFRAFORGE_VIEWPORT_PATH'
  const configured = process.env[envName]
  if (configured) {
    const resolved = path.resolve(configured)
    if (await existingFile(resolved)) return resolved
    throw new Error(`${envName} is not a file: ${resolved}`)
  }

  const executable = `infraforge-${kind}${process.platform === 'win32' ? '.exe' : ''}`
  const resourcesPath = process.resourcesPath
  const candidates = resourcesPath ? [
    path.join(resourcesPath, 'native', executable),
    path.join(resourcesPath, executable),
  ] : []
  for (const candidate of candidates) {
    const found = await existingFile(candidate)
    if (found) return found
  }
  return null
}
