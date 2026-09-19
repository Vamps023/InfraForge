import { describe, expect, it } from 'vitest'
import {
  workspaceRegistry,
  type WorkspaceDefinition,
} from './workspaceRegistry'
import { Mountain } from 'lucide-react'

describe('workspaceRegistry', () => {
  it('contains canonical built-in workspaces', () => {
    const all = workspaceRegistry.getAll()
    const ids = all.map((w) => w.id)
    expect(ids).toContain('home')
    expect(ids).toContain('world')
    expect(ids).toContain('terrain')
    expect(ids).toContain('roads')
  })

  it('filters visible workspaces in release mode', () => {
    const visible = workspaceRegistry.getVisible()
    const visibleIds = visible.map((w) => w.id)
    expect(visibleIds).toEqual(['home', 'world', 'terrain', 'roads'])
    // Hidden unreleased workspaces are excluded
    expect(visibleIds).not.toContain('rail')
    expect(visibleIds).not.toContain('simulation')
  })

  it('allows registering and unregistering custom workspace definitions', () => {
    const customDef: WorkspaceDefinition = {
      id: 'simulation',
      label: 'Simulation Custom',
      icon: Mountain,
      availability: {
        enabled: true,
        hidden: false,
      },
      toolGroups: [],
    }

    workspaceRegistry.register(customDef)
    expect(workspaceRegistry.get('simulation')?.label).toBe('Simulation Custom')

    workspaceRegistry.reset()
    expect(workspaceRegistry.get('simulation')?.availability.enabled).toBe(false)
  })

  it('resolves valid workspace fallback', () => {
    expect(workspaceRegistry.resolveValidWorkspace('roads', true)).toBe('roads')
    expect(workspaceRegistry.resolveValidWorkspace('nonexistent', true)).toBe('roads')
    expect(workspaceRegistry.resolveValidWorkspace('rail', true)).toBe('roads')
    expect(workspaceRegistry.resolveValidWorkspace(null, false)).toBe('home')
  })
})
