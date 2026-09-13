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

### Terrain — implemented local-file surface

- `terrain.probe_source` — read-only local source detection (format, CRS
  presence/resolution, raster geometry, elevation unit, NoData, size);
  missing CRS is a typed failure, never an assumption.
- `terrain.import_dataset` — starts the cancellable local-file
  `terrain.import` job (project-owned copy, validation, canonical commit,
  derived tile generation); returns `job_started`; progress via job events,
  cancellation via `job.cancel`.
- `terrain.list_datasets` / `terrain.get_dataset` — canonical dataset
  projections (the latter includes derived-tile cache presence).
- `terrain.sample` — canonical double-precision elevation at project-global
  coordinates with typed NoData/outside-coverage results.
- `terrain.regenerate_tiles` — re-generates missing/stale derived tiles as
  a cancellable `terrain.tiles` job.
- `terrain.get_scene` — renderer scene projection (metadata + session tile
  paths only; canonical terrain source data never crosses the wire).

### Terrain — required for Issue #6 completion (not implemented until production routes exist)

Issue #6 also requires `Download Area`: map draw → application selection
cells → native plan → selective provider acquisition → the same canonical
terrain ingestion/persistence/sampling/tile/renderer path used by local
files.

The protocol should expose production commands equivalent to the following
responsibilities. Final command names may change to match schema conventions,
but no placeholder route may be committed:

- `terrain.list_sources` — enumerate supported remote DEM providers and
  backend-observed capabilities/credential requirements. Never return or log
  secret credential values.
- `terrain.plan_download` — validate a provider plus the selected application
  terrain cells and return a deterministic acquisition plan: normalized
  selected coverage, unique/deduplicated provider requests, effective
  resolution, coverage warnings, request count, and estimated bytes when
  knowable. This command performs planning only and does not mutate canonical
  project terrain.
- `terrain.download_selected` — starts a cancellable long-running remote
  acquisition job for the exact planned/selected coverage. The engine owns
  provider networking, retry classification, decoding, NoData handling,
  clipping/resampling, project-owned source construction, provenance/hash,
  and canonical commit. It returns a background job id immediately.

#### Selective-download command rules

- Application `TerrainSelectionTile` cells and provider request tiles/areas
  are different concepts; provider requests are derived natively from selected
  application coverage.
- Selected application cells are authoritative. If only 3 of 20 cells are
  selected, the planner/executor must request only provider data required for
  those 3 cells when the provider supports selective access.
- Shared provider requests across adjacent selected cells are deduplicated.
- Disconnected selections are valid; unselected gaps must remain absent from
  canonical coverage, sampling, derived terrain tiles, and the Vulkan scene.
- If an upstream provider only supports bounding-area requests, unavoidable
  overfetch must be minimized and surfaced by the plan rather than hidden.
- Unit/CI tests use deterministic mock/local provider responses and do not
  depend on public internet access.
- Downloaded terrain becomes project-owned canonical source data; reopening a
  project must not require contacting the provider again.
- Credentials are runtime secrets and must never be persisted in project files,
  hard-coded, or included in logs/errors.

### Validation/jobs

- `world.check`
- `job.cancel` — cooperative cancellation of queued/running jobs
- `job.list` — job records for the Operations view

Domain commands are added with their production domain implementation. A
command name must not be committed with a production route that only returns
a placeholder success. Documentation may describe required future commands
only when they are explicitly marked not implemented, as above.

## Command handling rule

`transport decode -> validation -> application command handler -> domain/persistence transaction -> result/event`

Transport code cannot mutate project state directly.
