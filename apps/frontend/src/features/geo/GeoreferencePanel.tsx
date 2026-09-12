import { useEffect, useState, type FormEvent } from 'react'
import { create } from '@bufbuild/protobuf'
import { AxisConvention, GeoreferenceConfigSchema } from '@infraforge/protocol'
import type { EngineClient } from '../../lib/engineSession'
import { applyGeoreference, refreshGeoreference } from './geoApi'
import { useGeoStore } from './geoStore'
import { useProjectStore } from '../project/projectStore'

interface GeoreferencePanelProps {
  client: EngineClient
  onClose: () => void
}

// Project settings surface for the canonical georeference. All reads and
// writes go through the engine's Geo application service; the panel only
// edits transient form state.
export function GeoreferencePanel({ client, onClose }: GeoreferencePanelProps) {
  const info = useGeoStore((state) => state.info)
  const applying = useGeoStore((state) => state.applying)
  const geoError = useGeoStore((state) => state.lastError)
  const summary = useProjectStore((state) => state.summary)

  const [horizontalCrs, setHorizontalCrs] = useState('')
  const [linearUnit, setLinearUnit] = useState('')
  const [originEasting, setOriginEasting] = useState('0')
  const [originNorthing, setOriginNorthing] = useState('0')
  const [originHeight, setOriginHeight] = useState('0')
  const [verticalCrs, setVerticalCrs] = useState('')
  const [formError, setFormError] = useState<string | null>(null)

  useEffect(() => {
    void refreshGeoreference(client).catch(() => undefined)
  }, [client])

  useEffect(() => {
    const config = info?.config
    if (!config) {
      return
    }
    setHorizontalCrs(config.horizontalCrs)
    setLinearUnit(config.linearUnit)
    setOriginEasting(String(config.originEasting))
    setOriginNorthing(String(config.originNorthing))
    setOriginHeight(String(config.originHeight))
    setVerticalCrs(config.verticalCrs)
  }, [info])

  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setFormError(null)

    const easting = Number(originEasting)
    const northing = Number(originNorthing)
    const height = Number(originHeight)
    if (!Number.isFinite(easting) || !Number.isFinite(northing) || !Number.isFinite(height)) {
      setFormError('Origin values must be finite numbers.')
      return
    }
    if (horizontalCrs.trim().length === 0 || linearUnit.trim().length === 0) {
      setFormError('Horizontal CRS and linear unit are required.')
      return
    }

    const config = create(GeoreferenceConfigSchema, {
      horizontalCrs: horizontalCrs.trim(),
      linearUnit: linearUnit.trim(),
      axisConvention: AxisConvention.EASTING_NORTHING_UP,
      originEasting: easting,
      originNorthing: northing,
      originHeight: height,
      verticalCrs: verticalCrs.trim(),
    })

    try {
      await applyGeoreference(client, config, summary ? summary.revision : undefined)
    } catch {
      // The typed failure is projected through geoStore.lastError.
    }
  }

  const crs = info?.horizontalCrs
  const vertical = info?.verticalReference

  return (
    <div className="dialog-overlay" role="presentation">
      <form className="dialog" role="dialog" aria-modal="true" aria-label="Georeference" onSubmit={submit}>
        <h2>Georeference</h2>
        <p className="form-static">
          The canonical coordinate reference for this project, resolved by the native Geo service.
          Changing it is a project-level operation and advances the project revision.
        </p>

        {crs ? (
          <div className="form-static">
            Resolved: {crs.identifier || 'custom'} — {crs.name} ({crs.kind}), axis unit{' '}
            {crs.axisUnitToMetre.toPrecision(12)} m; linear unit {info?.linearUnitName} (
            {info?.linearUnitToMetre.toPrecision(12)} m); vertical reference{' '}
            {vertical?.present
              ? `${vertical.identifier} — ${vertical.name} (${
                  vertical.transformSupported
                    ? 'datum transforms via PROJ'
                    : 'datum transforms unavailable'
                })`
              : 'none'}
            .
          </div>
        ) : null}

        <div className="form-row-pair">
          <label className="form-row">
            <span className="form-label">Horizontal CRS</span>
            <input
              className="form-input"
              value={horizontalCrs}
              onChange={(event) => setHorizontalCrs(event.target.value)}
              placeholder="EPSG:32633"
              required
            />
          </label>
          <label className="form-row">
            <span className="form-label">Linear unit</span>
            <input
              className="form-input"
              value={linearUnit}
              onChange={(event) => setLinearUnit(event.target.value)}
              placeholder="metre"
              required
            />
          </label>
        </div>

        <div className="form-row-pair">
          <label className="form-row">
            <span className="form-label">Origin easting</span>
            <input
              className="form-input"
              value={originEasting}
              onChange={(event) => setOriginEasting(event.target.value)}
              required
            />
          </label>
          <label className="form-row">
            <span className="form-label">Origin northing</span>
            <input
              className="form-input"
              value={originNorthing}
              onChange={(event) => setOriginNorthing(event.target.value)}
              required
            />
          </label>
        </div>

        <div className="form-row-pair">
          <label className="form-row">
            <span className="form-label">Origin height</span>
            <input
              className="form-input"
              value={originHeight}
              onChange={(event) => setOriginHeight(event.target.value)}
              required
            />
          </label>
          <label className="form-row">
            <span className="form-label">Vertical CRS (optional)</span>
            <input
              className="form-input"
              value={verticalCrs}
              onChange={(event) => setVerticalCrs(event.target.value)}
              placeholder="EPSG:3855"
            />
          </label>
        </div>

        {formError ? (
          <p className="form-error" role="alert">
            {formError}
          </p>
        ) : null}
        {geoError ? (
          <p className="form-error" role="alert">
            {geoError.message}
          </p>
        ) : null}

        <div className="dialog-actions">
          <button className="button secondary" type="button" onClick={onClose} disabled={applying}>
            Close
          </button>
          <button className="button primary" type="submit" disabled={applying}>
            {applying ? 'Applying…' : 'Apply georeference'}
          </button>
        </div>
      </form>
    </div>
  )
}
