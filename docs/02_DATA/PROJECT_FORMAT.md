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

Contains small project-level metadata needed before opening the database, including:

- format identifier (`infraforge-project`);
- project format version;
- project UUID;
- display name;
- created/modified timestamps;
- database relative path;
- minimum compatible application/project-schema information;
- georeference summary required for safe project discovery/display.

It does not duplicate full roads, lanes, terrain, or simulation state.

## `project.db`

Contains canonical structured entities, relationships, revisions, migrations, configuration, and references to file-backed data.

## File-backed content

Large raster, imagery, model, texture, and generated chunk/cache payloads are files. Canonical records store stable logical identity, provenance, hash/metadata, and relative project path where appropriate.

## Cache rule

`cache/` is non-canonical. Deleting it may cost rebuild time but must not remove user-authored semantic data.

## Portability

Canonical paths stored in project data are project-relative where content belongs to the project. External-source references must be marked external and cannot be silently converted into project-owned content.

## Save semantics

A save is complete only after the database transaction and required durable metadata updates succeed. UI success is emitted after completion, never at operation start.