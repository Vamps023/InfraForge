# Command model

Commands are versioned application requests. Names are domain-qualified.

## Foundation command families

### Session

- `session.hello`
- `session.ping`

### Project lifecycle

- `project.create`
- `project.open`
- `project.save`
- `project.save_as`
- `project.close`
- `project.get_summary`

### Georeference

- `geo.get_georeference` — resolved canonical georeference (config + PROJ
  metadata + vertical reference info).
- `geo.set_georeference` — canonical update; `expected_revision` guards
  against lost updates; emits `georeference_changed` + revision/dirty events.
- `geo.transform_to_project_global` — source-CRS coordinates → project
  global coordinates through the Geo service (no client-side math). Source
  coordinates use normalized GIS-friendly axis order (longitude/latitude
  for geographic sources, easting/northing for projected); `source_crs`
  must be a geographic/projected/engineering CRS and
  `source_vertical_crs` a vertical CRS.

### Editing support

- `edit.undo`
- `edit.redo`
- `selection.resolve` for canonical selection details where required

### Terrain

- `terrain.probe_source` — read-only source detection (format, CRS
  presence/resolution, raster geometry, elevation unit, NoData, size);
  missing CRS is a typed failure, never an assumption.
- `terrain.import_dataset` — starts the cancellable `terrain.import` job
  (project-owned copy, validation, canonical commit, tile generation);
  returns `job_started`; progress via job events, cancellation via
  `job.cancel`.
- `terrain.list_datasets` / `terrain.get_dataset` — canonical dataset
  projections (the latter includes derived-tile cache presence).
- `terrain.sample` — canonical double-precision elevation at
  project-global coordinates (bilinear over cell centers; typed NoData /
  outside-coverage results).
- `terrain.regenerate_tiles` — re-generates missing/stale derived tiles as
  a cancellable `terrain.tiles` job.
- `terrain.get_scene` — renderer scene projection (metadata + session
  tile paths only; the canonical raster never crosses the wire).

### Validation/jobs

- `world.check`
- `job.cancel` — cooperative cancellation of queued/running jobs
- `job.list` — job records for the Operations view

Domain commands are added with their production domain implementation. A command name must not be committed with a production route that only returns a placeholder success.

## Command handling rule

`transport decode -> validation -> application command handler -> domain/persistence transaction -> result/event`

Transport code cannot mutate project state directly.