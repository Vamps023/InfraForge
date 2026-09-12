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

### Validation/jobs

- `world.check`
- `job.cancel`

Domain commands are added with their production domain implementation. A command name must not be committed with a production route that only returns a placeholder success.

## Command handling rule

`transport decode -> validation -> application command handler -> domain/persistence transaction -> result/event`

Transport code cannot mutate project state directly.