# Project format

## Project directory

InfraForge projects are directories with the `.iforge` suffix:

```text
Example.iforge/
├── project.json
├── project.db
├── assets/
│   ├── models/
│   ├── textures/
│   └── materials/
├── terrain/
│   ├── elevation/
│   └── imagery/
├── scenarios/
├── cache/
├── autosave/
└── logs/
```

## `project.json`

Contains small, immutable project-level metadata needed before opening the database, including:

- format identifier (`infraforge-project`);
- project format version;
- project UUID;
- display name;
- creation timestamp;
- database relative path;
- minimum compatible application version;
- georeference summary required for safe project discovery/display.

It does not duplicate mutable canonical state. Revision, saved-revision,
modified time, and the schema version are owned by `project.db` alone, so
migrations and saves never have to synchronize two files. The manifest is
rewritten only when project identity or discovery metadata changes —
creation, save-as, or a canonical georeference update (the georeference is
discovery metadata for safe project display) — never by an ordinary save.
The georeference block includes `horizontalCrs`, `linearUnit`,
`axisConvention`, `originEasting`, `originNorthing`, `originHeight`, and
`verticalCrs`; the manifest copy is verified against the `project.db` row on
open, so the two files can never disagree silently.

A georeference update writes the manifest first and commits the database
row in a transaction second. If the process is interrupted in between, the
manifest is ahead of the canonical database. On open the engine applies a
deterministic repair: `project.db` is the authority, the manifest's
georeference is rewritten from the database row, and the divergence is
logged (`project.manifest_georeference_repaired`). All other manifest/DB
identity divergences (UUID, display name, creation timestamp) still fail
the open as corruption — they have no in-flight update pattern that could
explain them.

## `project.db`

Contains canonical structured entities, relationships, revisions, migrations, configuration, and references to file-backed data.

## File-backed content

Large raster, imagery, model, texture, and generated chunk/cache payloads are files. Canonical records store stable logical identity, provenance, hash/metadata, and relative project path where appropriate.

## Cache rule

`cache/` is non-canonical. Deleting it may cost rebuild time but must not remove user-authored semantic data. Imported terrain rasters are NOT cache: `terrain/elevation/<uuid>.tif` is the project-owned canonical copy referenced by the `terrain_datasets` record (`docs/05_DOMAINS/TERRAIN.md`); only the derived tile files under `cache/terrain/` are rebuildable.

## Portability

Canonical paths stored in project data are project-relative where content belongs to the project. External-source references must be marked external and cannot be silently converted into project-owned content.

## Save semantics

A save is a single SQLite transaction (`saved_revision`, `modified_at`); it
either fully lands or fails without half-persisted state. `project.json` is
not rewritten by an ordinary save, so a failed save cannot leave the
manifest and database disagreeing. UI success is emitted after the
transaction commits, never at operation start.