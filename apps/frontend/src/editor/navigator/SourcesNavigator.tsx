import { useState } from 'react'
import { Database, FileText, Globe, Search, ShieldCheck } from 'lucide-react'
import { useTerrainStore } from '../../features/terrain/terrainStore'
import { useRoadStore } from '../../features/road/roadStore'
import { useProjectStore } from '../../features/project/projectStore'
import { useSelectionStore } from '../selection/selectionStore'

// SourcesNavigator — renders imported GIS/source provenance, attribution,
// bounds, CRS, and relink/verification information according to docs/06_UI_UX/WORKSPACE_MODEL.md.
// Shows only real provenance data: canonical CRS context, DEM raster sources,
// and road alignment source polyline metadata.
export function SourcesNavigator() {
  const [filter, setFilter] = useState('')
  const summary = useProjectStore((state) => state.summary)
  const terrainDatasets = useTerrainStore((state) => state.datasets)
  const roads = useRoadStore((state) => state.roads)
  const selectedIds = useSelectionStore((state) => state.selectedIds)
  const select = useSelectionStore((state) => state.select)

  const query = filter.trim().toLowerCase()

  const filteredTerrain = terrainDatasets.filter(
    (d) =>
      !query ||
      d.displayName.toLowerCase().includes(query) ||
      d.storagePath.toLowerCase().includes(query) ||
      d.sourceCrs.toLowerCase().includes(query) ||
      (d.sourceAttribution && d.sourceAttribution.toLowerCase().includes(query)),
  )

  const filteredRoads = roads.filter(
    (r) =>
      !query ||
      r.name.toLowerCase().includes(query) ||
      (r.sourceProvider && r.sourceProvider.toLowerCase().includes(query)) ||
      (r.sourceId && r.sourceId.toLowerCase().includes(query)),
  )

  return (
    <div className="sources-navigator" aria-label="Project sources and provenance">
      <div className="outliner-search">
        <Search size={14} className="outliner-search-icon" aria-hidden="true" />
        <input
          type="search"
          className="outliner-search-input"
          placeholder="Filter sources…"
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
          aria-label="Filter sources"
        />
      </div>

      <div className="sources-list" role="tree" aria-label="Sources list">
        {/* Project Spatial Reference Source */}
        {summary ? (
          <div className="source-group">
            <div className="source-group-header">
              <Globe size={13} />
              <span>Project Georeference</span>
            </div>
            <div
              className={`source-item${selectedIds.includes(summary.projectUuid) ? ' selected' : ''}`}
              role="treeitem"
              tabIndex={0}
              onClick={() => select([summary.projectUuid])}
              onKeyDown={(e) => {
                if (e.key === 'Enter' || e.key === ' ') {
                  select([summary.projectUuid])
                }
              }}
            >
              <div className="source-item-title">{summary.displayName}</div>
              <div className="source-item-meta mono">CRS: {summary.georeference?.horizontalCrs || '—'}</div>
              {summary.georeference ? (
                <div className="source-item-meta mono">
                  Origin: ({summary.georeference.originEasting.toFixed(2)}, {summary.georeference.originNorthing.toFixed(2)}, {summary.georeference.originHeight.toFixed(2)})
                </div>
              ) : null}
            </div>
          </div>
        ) : null}

        {/* Terrain DEM Sources */}
        <div className="source-group">
          <div className="source-group-header">
            <Database size={13} />
            <span>Terrain DEM Sources ({filteredTerrain.length})</span>
          </div>
          {filteredTerrain.length === 0 ? (
            <div className="source-empty">No terrain sources imported</div>
          ) : (
            filteredTerrain.map((d) => {
              const canonicalId = `terrain:${d.datasetUuid}`
              const isSelected = selectedIds.includes(canonicalId)
              return (
                <div
                  key={d.datasetUuid}
                  className={`source-item${isSelected ? ' selected' : ''}`}
                  role="treeitem"
                  tabIndex={0}
                  onClick={() => select([canonicalId])}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter' || e.key === ' ') {
                      select([canonicalId])
                    }
                  }}
                >
                  <div className="source-item-title">
                    <FileText size={12} />
                    <span>{d.displayName}</span>
                  </div>
                  <div className="source-item-path mono">{d.storagePath}</div>
                  <div className="source-item-meta mono">CRS: {d.sourceCrs}</div>
                  {d.sourceAttribution ? (
                    <div className="source-item-meta">Attribution: {d.sourceAttribution}</div>
                  ) : null}
                  <div className="source-item-meta mono">
                    Resolution: {d.cellSizeX.toFixed(2)} × {d.cellSizeY.toFixed(2)}
                  </div>
                </div>
              )
            })
          )}
        </div>

        {/* Road Alignment Sources */}
        <div className="source-group">
          <div className="source-group-header">
            <FileText size={13} />
            <span>Road Polyline Sources ({filteredRoads.length})</span>
          </div>
          {filteredRoads.length === 0 ? (
            <div className="source-empty">No road sources authored</div>
          ) : (
            filteredRoads.map((r) => {
              const canonicalId = `road:${r.roadId}`
              const isSelected = selectedIds.includes(canonicalId)
              return (
                <div
                  key={r.roadId}
                  className={`source-item${isSelected ? ' selected' : ''}`}
                  role="treeitem"
                  tabIndex={0}
                  onClick={() => select([canonicalId])}
                  onKeyDown={(e) => {
                    if (e.key === 'Enter' || e.key === ' ') {
                      select([canonicalId])
                    }
                  }}
                >
                  <div className="source-item-title">
                    <span>{r.name}</span>
                    {r.protectedAnchorCount > 0 ? (
                      <span className="source-badge">
                        <ShieldCheck size={10} />
                        {r.protectedAnchorCount} anchors
                      </span>
                    ) : null}
                  </div>
                  {r.sourceProvider ? (
                    <div className="source-item-meta">Provider: {r.sourceProvider}</div>
                  ) : null}
                  {r.sourceId ? (
                    <div className="source-item-meta mono">Source ID: {r.sourceId}</div>
                  ) : null}
                  <div className="source-item-meta mono">Length: {r.length.toFixed(2)} units</div>
                </div>
              )
            })
          )}
        </div>
      </div>
    </div>
  )
}
