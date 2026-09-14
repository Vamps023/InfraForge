import { inspectorSectionRegistry, type InspectorSectionContext } from '../../editor/inspector/inspectorRegistry'
import { useTerrainStore } from './terrainStore'
import { refreshDatasetDetails } from './terrainApi'
import type { EngineClient } from '../../lib/engineSession'

// Terrain inspector section: shows real canonical terrain metadata for the
// selected dataset through the Issue #5 inspector section registry.

export interface TerrainInspectorDeps {
  getEngineClient: () => EngineClient | null
}

export function registerTerrainInspectorSection(deps: TerrainInspectorDeps): void {
  const section = {
    id: 'terrain-dataset',
    label: 'Terrain Dataset',
    order: 50,
    applies: (context: InspectorSectionContext) => {
      const id = context.primaryId
      return id !== null && id.startsWith('terrain:')
    },
    render: (context: InspectorSectionContext) => {
      const id = context.primaryId
      if (!id || !id.startsWith('terrain:')) {
        return null
      }
      const datasetUuid = id.slice('terrain:'.length)
      const datasets = useTerrainStore.getState().datasets
      const dataset = datasets.find((d) => d.datasetUuid === datasetUuid)

      if (!dataset) {
        return null
      }

      // Trigger a details refresh in the background (tile counts).
      const client = deps.getEngineClient()
      if (client) {
        void refreshDatasetDetails(client, datasetUuid).catch(() => undefined)
      }

      return (
        <div className="inspector-section terrain-inspector">
          <dl className="inspector-fields">
            <dt>Dataset ID</dt>
            <dd className="mono">{dataset.datasetUuid}</dd>
            <dt>Display Name</dt>
            <dd>{dataset.displayName}</dd>
            <dt>Source CRS</dt>
            <dd className="mono">{dataset.sourceCrs}</dd>
            <dt>Raster Dimensions</dt>
            <dd>
              {dataset.rasterWidth.toString()} × {dataset.rasterHeight.toString()} px
            </dd>
            <dt>Cell Size</dt>
            <dd>
              {dataset.cellSizeX.toPrecision(6)} × {dataset.cellSizeY.toPrecision(6)}{' '}
              {dataset.elevationUnit === 'metre' ? 'm' : 'units'}
            </dd>
            <dt>Elevation Unit</dt>
            <dd>{dataset.elevationUnit}</dd>
            <dt>Elevation Range</dt>
            <dd>
              {dataset.minZ.toPrecision(6)} – {dataset.maxZ.toPrecision(6)} m
            </dd>
            <dt>Canonical Bounds</dt>
            <dd className="mono small">
              E: {dataset.boundsWest.toPrecision(6)} – {dataset.boundsEast.toPrecision(6)}
              <br />
              N: {dataset.boundsSouth.toPrecision(6)} – {dataset.boundsNorth.toPrecision(6)}
            </dd>
            <dt>NoData</dt>
            <dd>{dataset.hasNodata ? `present (${dataset.nodataValue})` : 'none'}</dd>
            <dt>Storage Path</dt>
            <dd className="mono small">{dataset.storagePath}</dd>
            {dataset.sourceAttribution ? (
              <>
                <dt>Source Attribution</dt>
                <dd className="small">{dataset.sourceAttribution}</dd>
              </>
            ) : null}
            <dt>Revision</dt>
            <dd>{dataset.revision.toString()}</dd>
            {dataset.diagnostics.length > 0 ? (
              <>
                <dt>Diagnostics</dt>
                <dd>
                  <ul className="terrain-diagnostics">
                    {dataset.diagnostics.map((diag, i) => (
                      <li key={i}>
                        {diag.code}: {diag.message}
                      </li>
                    ))}
                  </ul>
                </dd>
              </>
            ) : null}
          </dl>
        </div>
      )
    },
  }

  inspectorSectionRegistry.register(section)
}

export function unregisterTerrainInspectorSection(): void {
  inspectorSectionRegistry.unregister('terrain-dataset')
}
