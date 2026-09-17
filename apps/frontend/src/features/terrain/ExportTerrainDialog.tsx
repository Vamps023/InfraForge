import { useState } from 'react'
import { CheckCircle2, FileWarning, FolderOpen, X } from 'lucide-react'
import {
  TerrainExportFormat,
  TerrainAlbedoExportFormat,
  type TerrainExportResult,
} from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import { exportTerrain } from './terrainApi'
import { useTerrainStore } from './terrainStore'

interface ExportTerrainDialogProps {
  client: EngineClient
  busy: boolean
  onClose: () => void
}

function formatBytes(bytes: bigint): string {
  const value = Number(bytes)
  if (!Number.isFinite(value) || value <= 0) return '—'
  if (value >= 1024 * 1024) return `${(value / (1024 * 1024)).toFixed(1)} MB`
  if (value >= 1024) return `${(value / 1024).toFixed(1)} KB`
  return `${value} B`
}

export function ExportTerrainDialog({ client, busy, onClose }: ExportTerrainDialogProps) {
  const datasets = useTerrainStore((state) => state.datasets)
  const [selectedDatasetUuid, setSelectedDatasetUuid] = useState<string>(
    datasets[0]?.datasetUuid ?? '',
  )
  const [outputDirectory, setOutputDirectory] = useState('')
  const [heightmapFormat, setHeightmapFormat] = useState<TerrainExportFormat>(
    TerrainExportFormat.GEOTIFF_FLOAT32,
  )
  const [albedoFormat, setAlbedoFormat] = useState<TerrainAlbedoExportFormat>(
    TerrainAlbedoExportFormat.NONE,
  )
  const [targetResolution, setTargetResolution] = useState<number>(0)
  const [targetCrs, setTargetCrs] = useState<string>('auto')
  const [exporting, setExporting] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [result, setResult] = useState<TerrainExportResult | null>(null)

  const selectedDataset = datasets.find((d) => d.datasetUuid === selectedDatasetUuid)

  const pickDirectory = async () => {
    const desktop = window.infraforgeDesktop
    if (!desktop?.pickDirectory) {
      return
    }
    try {
      const chosen = await desktop.pickDirectory({
        title: 'Select Terrain Export Directory',
        buttonLabel: 'Select Export Directory',
      })
      if (chosen) {
        setOutputDirectory(chosen)
      }
    } catch {
      // Ignored
    }
  }

  const handleExport = async (e: React.FormEvent) => {
    e.preventDefault()
    if (!selectedDatasetUuid) {
      setError('Please select a terrain dataset to export.')
      return
    }
    if (!outputDirectory.trim()) {
      setError('Please specify an export destination directory.')
      return
    }

    setExporting(true)
    setError(null)
    setResult(null)

    try {
      const exportOutput = await exportTerrain(client, {
        datasetUuid: selectedDatasetUuid,
        outputDirectory: outputDirectory.trim(),
        heightmapFormat,
        albedoFormat,
        targetResolution,
        targetCrs,
      })
      setResult(exportOutput)
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Terrain export failed')
    } finally {
      setExporting(false)
    }
  }

  return (
    <div className="dialog-overlay" role="presentation">
      <div
        className="dialog dialog-terrain"
        role="dialog"
        aria-modal="true"
        aria-label="Export terrain"
      >
        <div className="dialog-header">
          <h2>Export terrain</h2>
          <button
            type="button"
            className="dialog-close"
            aria-label="Close dialog"
            onClick={onClose}
          >
            <X size={16} />
          </button>
        </div>

        <div className="dialog-body">
          {datasets.length === 0 ? (
            <div className="form-error" role="alert">
              <FileWarning size={14} /> No imported terrain datasets available in this project.
            </div>
          ) : (
            <form onSubmit={handleExport}>
              <div className="form-row">
                <span className="form-label">Dataset</span>
                <select
                  className="form-input"
                  value={selectedDatasetUuid}
                  onChange={(e) => setSelectedDatasetUuid(e.target.value)}
                  disabled={exporting}
                >
                  {datasets.map((d) => (
                    <option key={d.datasetUuid} value={d.datasetUuid}>
                      {d.displayName} ({d.rasterWidth} × {d.rasterHeight} px)
                    </option>
                  ))}
                </select>
              </div>

              {selectedDataset && (
                <div className="metadata-card" style={{ marginBottom: '12px' }}>
                  <div className="metadata-card-title">Dataset details</div>
                  <dl className="metadata-grid">
                    <div className="metadata-field">
                      <dt>CRS</dt>
                      <dd>{selectedDataset.sourceCrs}</dd>
                    </div>
                    <div className="metadata-field">
                      <dt>Elevation Range</dt>
                      <dd>
                        {selectedDataset.minZ.toFixed(1)} to {selectedDataset.maxZ.toFixed(1)}{' '}
                        {selectedDataset.elevationUnit || 'm'}
                      </dd>
                    </div>
                    <div className="metadata-field">
                      <dt>Cell Size</dt>
                      <dd>
                        {selectedDataset.cellSizeX.toFixed(2)} × {selectedDataset.cellSizeY.toFixed(2)} m
                      </dd>
                    </div>
                    <div className="metadata-field">
                      <dt>Native Dimensions</dt>
                      <dd>
                        {selectedDataset.rasterWidth} × {selectedDataset.rasterHeight} px
                      </dd>
                    </div>
                  </dl>
                </div>
              )}

              <div className="form-row">
                <span className="form-label">Destination</span>
                <div style={{ display: 'flex', gap: '8px', flex: 1 }}>
                  <input
                    type="text"
                    className="form-input"
                    value={outputDirectory}
                    onChange={(e) => setOutputDirectory(e.target.value)}
                    placeholder="Output folder path..."
                    disabled={exporting}
                    style={{ flex: 1 }}
                  />
                  <button
                    type="button"
                    className="button secondary"
                    onClick={() => void pickDirectory()}
                    disabled={exporting}
                    title="Browse for folder"
                  >
                    <FolderOpen size={14} /> Browse
                  </button>
                </div>
              </div>

              <div className="form-row">
                <span className="form-label">Heightmap Format</span>
                <select
                  className="form-input"
                  value={heightmapFormat}
                  onChange={(e) => setHeightmapFormat(Number(e.target.value))}
                  disabled={exporting}
                >
                  <option value={TerrainExportFormat.GEOTIFF_FLOAT32}>
                    GeoTIFF Float32 (Standard 32-bit Floating Point DEM)
                  </option>
                  <option value={TerrainExportFormat.GEOTIFF_INT16}>
                    GeoTIFF Int16 (16-bit Signed Meters Elevation)
                  </option>
                  <option value={TerrainExportFormat.GEOTIFF_UINT16}>
                    GeoTIFF UInt16 (16-bit Normalized DEM)
                  </option>
                  <option value={TerrainExportFormat.PNG_16}>
                    16-bit Grayscale PNG (Unreal Engine / Unity / Blender)
                  </option>
                  <option value={TerrainExportFormat.RAW_R16}>
                    Raw R16 (16-bit Little-Endian RAW Heightmap)
                  </option>
                </select>
              </div>

              <div className="form-row">
                <span className="form-label">Satellite Albedo</span>
                <select
                  className="form-input"
                  value={albedoFormat}
                  onChange={(e) => setAlbedoFormat(Number(e.target.value))}
                  disabled={exporting}
                >
                  <option value={TerrainAlbedoExportFormat.NONE}>None (Elevation Only)</option>
                  <option value={TerrainAlbedoExportFormat.PNG_RGB}>PNG RGB (Color Texture)</option>
                  <option value={TerrainAlbedoExportFormat.GEOTIFF_RGB}>GeoTIFF RGB (Georeferenced Orthophoto)</option>
                </select>
              </div>

              <div className="form-row">
                <span className="form-label">Resolution</span>
                <select
                  className="form-input"
                  value={targetResolution}
                  onChange={(e) => setTargetResolution(Number(e.target.value))}
                  disabled={exporting}
                >
                  <option value={0}>Native (Source Resolution)</option>
                  <option value={512}>512 × 512 px</option>
                  <option value={1024}>1024 × 1024 px</option>
                  <option value={2048}>2048 × 2048 px</option>
                  <option value={4096}>4096 × 4096 px</option>
                </select>
              </div>

              <div className="form-row">
                <span className="form-label">Target CRS</span>
                <select
                  className="form-input"
                  value={targetCrs}
                  onChange={(e) => setTargetCrs(e.target.value)}
                  disabled={exporting}
                >
                  <option value="auto">Auto (Source / Project CRS)</option>
                  <option value="EPSG:4326">WGS84 (EPSG:4326)</option>
                  <option value="EPSG:3857">Web Mercator (EPSG:3857)</option>
                </select>
              </div>

              {result && (
                <div className="form-static success" role="status" style={{ marginTop: '12px' }}>
                  <p style={{ display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600 }}>
                    <CheckCircle2 size={16} /> Export completed successfully!
                  </p>
                  <p style={{ margin: '4px 0' }}>
                    Manifest: <code>{result.manifestPath}</code>
                  </p>
                  <p style={{ margin: '4px 0' }}>
                    Total size: {formatBytes(result.totalBytes)} ({result.exportedFiles.length} files generated)
                  </p>
                </div>
              )}

              {error && (
                <div className="form-error" role="alert" style={{ marginTop: '12px' }}>
                  <FileWarning size={14} /> {error}
                </div>
              )}

              <div className="dialog-actions" style={{ marginTop: '16px' }}>
                <button className="button secondary" type="button" onClick={onClose}>
                  {result ? 'Close' : 'Cancel'}
                </button>
                <button
                  className="button primary"
                  type="submit"
                  disabled={exporting || busy || !outputDirectory.trim()}
                >
                  {exporting ? 'Exporting…' : 'Export Terrain'}
                </button>
              </div>
            </form>
          )}
        </div>
      </div>
    </div>
  )
}
