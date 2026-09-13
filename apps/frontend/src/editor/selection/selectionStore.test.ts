import { beforeEach, describe, expect, it } from 'vitest'
import { useSelectionStore } from './selectionStore'

beforeEach(() => {
  useSelectionStore.getState().clear()
})

describe('selectionStore', () => {
  it('starts empty', () => {
    const state = useSelectionStore.getState()
    expect(state.selectedIds).toEqual([])
    expect(state.primaryId).toBeNull()
  })

  it('replace mode sets the selection and primary', () => {
    useSelectionStore.getState().select(['a', 'b'], 'replace')
    const state = useSelectionStore.getState()
    expect(state.selectedIds).toEqual(['a', 'b'])
    expect(state.primaryId).toBe('b')
  })

  it('replace deduplicates ids', () => {
    useSelectionStore.getState().select(['a', 'a', 'b'], 'replace')
    expect(useSelectionStore.getState().selectedIds).toEqual(['a', 'b'])
  })

  it('replace with empty list clears primary', () => {
    useSelectionStore.getState().select(['a'], 'replace')
    useSelectionStore.getState().select([], 'replace')
    expect(useSelectionStore.getState().selectedIds).toEqual([])
    expect(useSelectionStore.getState().primaryId).toBeNull()
  })

  it('add mode merges without duplicates and keeps existing primary when ids are new', () => {
    useSelectionStore.getState().select(['a'], 'replace')
    useSelectionStore.getState().select(['b', 'a'], 'add')
    const state = useSelectionStore.getState()
    expect(state.selectedIds).toEqual(['a', 'b'])
    expect(state.primaryId).toBe('b')
  })

  it('toggle mode removes existing ids and adds new ones', () => {
    useSelectionStore.getState().select(['a', 'b'], 'replace')
    useSelectionStore.getState().select(['b', 'c'], 'toggle')
    const state = useSelectionStore.getState()
    expect(state.selectedIds).toEqual(['a', 'c'])
    expect(state.primaryId).toBe('c')
  })

  it('toggle demotes primary when it is removed', () => {
    useSelectionStore.getState().select(['a', 'b'], 'replace')
    useSelectionStore.getState().select(['a'], 'toggle')
    const state = useSelectionStore.getState()
    expect(state.selectedIds).toEqual(['b'])
    expect(state.primaryId).toBe('b')
  })

  it('toggle() helper toggles a single id', () => {
    useSelectionStore.getState().toggle('a')
    expect(useSelectionStore.getState().selectedIds).toEqual(['a'])
    useSelectionStore.getState().toggle('a')
    expect(useSelectionStore.getState().selectedIds).toEqual([])
    expect(useSelectionStore.getState().primaryId).toBeNull()
  })

  it('setPrimary only accepts an id in the current selection', () => {
    useSelectionStore.getState().select(['a', 'b'], 'replace')
    useSelectionStore.getState().setPrimary('a')
    expect(useSelectionStore.getState().primaryId).toBe('a')
    useSelectionStore.getState().setPrimary('zzz')
    expect(useSelectionStore.getState().primaryId).toBe('a')
    useSelectionStore.getState().setPrimary(null)
    expect(useSelectionStore.getState().primaryId).toBeNull()
  })

  it('clear resets selection and primary', () => {
    useSelectionStore.getState().select(['a'], 'replace')
    useSelectionStore.getState().clear()
    expect(useSelectionStore.getState().selectedIds).toEqual([])
    expect(useSelectionStore.getState().primaryId).toBeNull()
  })
})
