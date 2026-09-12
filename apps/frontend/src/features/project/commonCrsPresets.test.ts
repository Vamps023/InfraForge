import { describe, expect, it } from 'vitest'
import {
  commonCrsPresets,
  crsPresetMetaLine,
  findCrsPreset,
  groupCrsPresets,
  searchCrsPresets,
} from './commonCrsPresets'

describe('common CRS presets', () => {
  it('exposes Common Global and Common UTM groups in display order', () => {
    const groups = groupCrsPresets(commonCrsPresets)
    expect(groups.map((group) => group.label)).toEqual(['Common Global CRSs', 'Common UTM CRSs'])
    expect(groups[0]?.items.map((preset) => preset.code)).toEqual(['EPSG:4326', 'EPSG:3857'])
    expect(groups[1]?.items.map((preset) => preset.code)).toEqual([
      'EPSG:32632',
      'EPSG:32633',
      'EPSG:32634',
      'EPSG:32643',
      'EPSG:32644',
      'EPSG:32645',
      'EPSG:32646',
      'EPSG:32647',
      'EPSG:32648',
    ])
  })

  it('marks EPSG:4326 as geographic and not allowed, EPSG:3857 as projected and allowed', () => {
    const geographic = findCrsPreset('EPSG:4326')
    expect(geographic?.kind).toBe('geographic')
    expect(geographic?.unit).toBe('degree')
    expect(geographic?.projectCrsAllowed).toBe(false)
    expect(crsPresetMetaLine(geographic!)).toContain('Not valid as Project CRS')

    const projected = findCrsPreset('EPSG:3857')
    expect(projected?.kind).toBe('projected')
    expect(projected?.unit).toBe('metre')
    expect(projected?.projectCrsAllowed).toBe(true)
    expect(crsPresetMetaLine(projected!)).toBe('Projected · metre')
  })

  it('searches by EPSG code', () => {
    expect(searchCrsPresets('4326').map((preset) => preset.code)).toEqual(['EPSG:4326'])
    expect(searchCrsPresets('EPSG:32643').map((preset) => preset.code)).toEqual(['EPSG:32643'])
  })

  it('searches by CRS name', () => {
    const results = searchCrsPresets('WGS 84').map((preset) => preset.code)
    expect(results).toContain('EPSG:4326')
    expect(results).toContain('EPSG:3857')
    expect(results).toContain('EPSG:32643')
    expect(results).toHaveLength(commonCrsPresets.length)
  })

  it('searches by UTM zone text', () => {
    expect(searchCrsPresets('UTM 43N').map((preset) => preset.code)).toEqual(['EPSG:32643'])
    expect(searchCrsPresets('zone 33').map((preset) => preset.code)).toEqual(['EPSG:32633'])
  })

  it('searches by category and kind text', () => {
    expect(searchCrsPresets('geographic').map((preset) => preset.code)).toEqual(['EPSG:4326'])
    expect(searchCrsPresets('utm').length).toBe(9)
    expect(searchCrsPresets('global').map((preset) => preset.code)).toEqual(['EPSG:4326', 'EPSG:3857'])
  })

  it('returns nothing for a query that matches no preset', () => {
    expect(searchCrsPresets('Gauss-Kruger')).toEqual([])
  })

  it('finds presets by exact code only through findCrsPreset', () => {
    expect(findCrsPreset(' EPSG:32643 ')?.name).toBe('WGS 84 / UTM Zone 43N')
    expect(findCrsPreset('EPSG:99999')).toBeNull()
    expect(findCrsPreset('proj-string')).toBeNull()
  })
})
