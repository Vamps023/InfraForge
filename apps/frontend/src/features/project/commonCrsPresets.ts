// Curated "Common CRS" picker presets. This is UI metadata only — it is NOT
// a canonical CRS model: the engine's Geo service remains the sole authority
// for CRS validation and canonicalization (issue #3 architecture). The
// picker only writes CRS identifier strings into the existing horizontalCrs
// field; a later "search all CRS" dialog can replace this list with an
// engine/PROJ-backed query without changing the component logic.

export type CommonCrsCategory = 'global' | 'utm'
export type CommonCrsKind = 'geographic' | 'projected' | 'engineering'

export interface CommonCrsPreset {
  code: string
  name: string
  category: CommonCrsCategory
  kind: CommonCrsKind
  unit: string
  projectCrsAllowed: boolean
}

export const commonCrsPresets: CommonCrsPreset[] = [
  {
    code: 'EPSG:4326',
    name: 'WGS 84 - Geographic',
    category: 'global',
    kind: 'geographic',
    unit: 'degree',
    projectCrsAllowed: false,
  },
  {
    code: 'EPSG:3857',
    name: 'WGS 84 / Pseudo-Mercator',
    category: 'global',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32632',
    name: 'WGS 84 / UTM Zone 32N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32633',
    name: 'WGS 84 / UTM Zone 33N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32634',
    name: 'WGS 84 / UTM Zone 34N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32643',
    name: 'WGS 84 / UTM Zone 43N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32644',
    name: 'WGS 84 / UTM Zone 44N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32645',
    name: 'WGS 84 / UTM Zone 45N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32646',
    name: 'WGS 84 / UTM Zone 46N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32647',
    name: 'WGS 84 / UTM Zone 47N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
  {
    code: 'EPSG:32648',
    name: 'WGS 84 / UTM Zone 48N',
    category: 'utm',
    kind: 'projected',
    unit: 'metre',
    projectCrsAllowed: true,
  },
]

export interface CommonCrsGroup {
  category: CommonCrsCategory
  label: string
  items: CommonCrsPreset[]
}

const groupLabels: Record<CommonCrsCategory, string> = {
  global: 'Common Global CRSs',
  utm: 'Common UTM CRSs',
}

// Non-empty groups in display order (global first, then UTM).
export function groupCrsPresets(presets: CommonCrsPreset[]): CommonCrsGroup[] {
  const order: CommonCrsCategory[] = ['global', 'utm']
  return order
    .map((category) => ({
      category,
      label: groupLabels[category],
      items: presets.filter((preset) => preset.category === category),
    }))
    .filter((group) => group.items.length > 0)
}

// Case-insensitive AND-match over the code, name, category, and kind text so
// "4326", "wgs 84", "UTM 43N", and "geographic" all find their entries.
export function searchCrsPresets(query: string, presets: CommonCrsPreset[] = commonCrsPresets): CommonCrsPreset[] {
  const terms = query.toLowerCase().split(/\s+/).filter(Boolean)
  if (terms.length === 0) {
    return presets
  }
  return presets.filter((preset) => {
    const haystack = `${preset.code} ${preset.name} ${preset.category} ${preset.kind}`.toLowerCase()
    return terms.every((term) => haystack.includes(term))
  })
}

// Finds the preset whose code equals the given value exactly; the field may
// also hold arbitrary manual text, in which case this returns null.
export function findCrsPreset(code: string, presets: CommonCrsPreset[] = commonCrsPresets): CommonCrsPreset | null {
  return presets.find((preset) => preset.code === code.trim()) ?? null
}

// Display metadata for the field caption; the geographic-invalid wording is
// the pre-submit hint — the engine remains the authority that rejects.
export function crsPresetMetaLine(preset: CommonCrsPreset): string {
  const unit = preset.unit === 'degree' ? 'degrees' : preset.unit
  const kind = preset.kind.charAt(0).toUpperCase() + preset.kind.slice(1)
  const line = `${kind} · ${unit}`
  return preset.projectCrsAllowed ? line : `${line} · Not valid as Project CRS`
}
