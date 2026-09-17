import { inspectorSectionRegistry, type InspectorSectionContext } from '../../editor/inspector/inspectorRegistry'
import { useTerrainStore } from './terrainStore'
import { fetchTerrainScene, refreshDatasetDetails } from './terrainApi'
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
    category: 'geometry' as const,
    order: 0,
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
          <button
            className="button secondary"
            type="button"
            disabled={!client || !window.infraforgeDesktop?.setViewportScene}
            onClick={() => {
              if (!client) return
              void fetchTerrainScene(client).then((scene) => {
                window.infraforgeDesktop?.setViewportScene?.(scene as Record<string, unknown>)
                window.infraforgeDesktop?.setViewportCamera?.('focus-terrain', datasetUuid)
              }).catch(() => undefined)
            }}
          >
            Focus Terrain
          </button>
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
            <dt>Source Pixel Size</dt>
            <dd>
              {dataset.cellSizeX.toPrecision(6)} × {dataset.cellSizeY.toPrecision(6)}{' '}
              {dataset.horizontalUnitSymbol || dataset.horizontalUnitName || 'units'}
            </dd>
            <dt>Elevation Unit</dt>
            <dd>{dataset.elevationUnit === 'unknown' ? 'Unknown' : dataset.elevationUnit}</dd>
            <dt>Elevation Range</dt>
            <dd>
              {dataset.minZ.toPrecision(6)} – {dataset.maxZ.toPrecision(6)} project units
            </dd>
            <dt>Project-global Bounds</dt>
            <dd className="mono small">
              E: {dataset.boundsWest.toPrecision(6)} – {dataset.boundsEast.toPrecision(6)}
              <br />
              N: {dataset.boundsSouth.toPrecision(6)} – {dataset.boundsNorth.toPrecision(6)}
              <br />
              (project linear units)
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
