import { beforeEach, describe, expect, it, vi } from 'vitest'
import { render, screen, fireEvent, waitFor } from '@testing-library/react'
import {
  TerrainExportFormat,
  TerrainAlbedoExportFormat,
  type TerrainDatasetInfo,
} from '@infraforge/protocol'
import { ExportTerrainDialog } from './ExportTerrainDialog'
import { exportTerrain } from './terrainApi'
import { useTerrainStore } from './terrainStore'
import type { EngineClient } from '../../lib/engineSession'

describe('terrain export workflow', () => {
  beforeEach(() => {
    useTerrainStore.getState().reset()
  })

  it('exportTerrain sends terrainExport command with correct parameters', async () => {
    const sendCommand = vi.fn().mockResolvedValue({
      case: 'terrainExportResult',
      value: {
        datasetUuid: 'dataset-123',
        manifestPath: '/exports/manifest.json',
        exportedFiles: ['/exports/terrain_heightmap.png', '/exports/manifest.json'],
        totalBytes: 10240n,
      },
    })
    const client = { sendCommand } as unknown as EngineClient

    const result = await exportTerrain(client, {
      datasetUuid: 'dataset-123',
      outputDirectory: '/exports',
      heightmapFormat: TerrainExportFormat.PNG_16,
      albedoFormat: TerrainAlbedoExportFormat.PNG_RGB,
      targetResolution: 2048,
      targetCrs: 'EPSG:3857',
    })

    expect(sendCommand).toHaveBeenCalledWith({
      case: 'terrainExport',
      value: expect.objectContaining({
        datasetUuid: 'dataset-123',
        outputDirectory: '/exports',
        heightmapFormat: TerrainExportFormat.PNG_16,
        albedoFormat: TerrainAlbedoExportFormat.PNG_RGB,
        targetResolution: 2048,
        targetCrs: 'EPSG:3857',
      }),
    })
    expect(result.manifestPath).toBe('/exports/manifest.json')
    expect(result.exportedFiles).toHaveLength(2)
  })

  it('renders ExportTerrainDialog and executes export successfully', async () => {
    const mockDataset: TerrainDatasetInfo = {
      $typeName: 'infraforge.protocol.v1.TerrainDatasetInfo',
      datasetUuid: 'test-dataset-uuid',
      displayName: 'Matterhorn DEM',
      storagePath: 'terrain/elevation/matterhorn.tif',
      sourceFormat: 'GTiff',
      sourceCrs: 'EPSG:32632',
      rasterWidth: 1024n,
      rasterHeight: 1024n,
      cellSizeX: 25.0,
      cellSizeY: 25.0,
      elevationUnit: 'metre',
      hasNodata: false,
      nodataValue: -9999,
      minZ: 1600.0,
      maxZ: 4478.0,
      boundsEast: 400000,
      boundsWest: 374400,
      boundsNorth: 5090000,
      boundsSouth: 5064400,
      revision: 1n,
      diagnostics: [],
      createdAt: '2026-09-17T00:00:00Z',
      sourceAttribution: 'Copernicus',
      horizontalUnitName: 'metre',
      horizontalUnitSymbol: 'm',
      horizontalUnitIsAngular: false,
      elevationUnitSource: 'declared',
      sampleScale: 1.0,
      sampleOffset: 0.0,
    }

    useTerrainStore.getState().setDatasets([mockDataset])

    const sendCommand = vi.fn().mockResolvedValue({
      case: 'terrainExportResult',
      value: {
        datasetUuid: 'test-dataset-uuid',
        manifestPath: 'D:/exports/manifest.json',
        exportedFiles: [
          'D:/exports/Matterhorn_DEM_heightmap.png',
          'D:/exports/manifest.json',
        ],
        totalBytes: 2097152n,
      },
    })
    const client = { sendCommand } as unknown as EngineClient
    const onClose = vi.fn()

    render(<ExportTerrainDialog client={client} busy={false} onClose={onClose} />)

    expect(screen.getByRole('heading', { name: /Export terrain/i })).toBeInTheDocument()
    expect(screen.getByText(/Matterhorn DEM \(1024 × 1024 px\)/i)).toBeInTheDocument()
    expect(screen.getByText(/1600.0 to 4478.0 metre/i)).toBeInTheDocument()

    const destinationInput = screen.getByPlaceholderText(/Output folder path\.\.\./i)
    fireEvent.change(destinationInput, { target: { value: 'D:/exports' } })

    const exportBtn = screen.getByRole('button', { name: /Export Terrain/i })
    fireEvent.click(exportBtn)

    await waitFor(() => {
      expect(screen.getByText(/Export completed successfully!/i)).toBeInTheDocument()
    })

    expect(screen.getByText(/D:\/exports\/manifest\.json/i)).toBeInTheDocument()
    expect(sendCommand).toHaveBeenCalled()
  })
})
