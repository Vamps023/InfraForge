# Project domain

The Project domain owns the canonical project lifecycle: create, open, save,
save-as, close, and summary query. It is the first canonical domain of the
engine; every other domain builds on an open project session.

## Production path

```text
Frontend action
    → generated protobuf command (contracts/proto/infraforge/protocol/v1/project.proto)
    → WebSocket (authenticated loopback session)
    → application executor (ProjectService, single-threaded)
    → persistence port (ProjectStore)
    → SQLite + project.json on disk
    → result frame + project.* events
    → frontend projections
```

## Project format

Projects are directories named `<name>.iforge` following
`docs/02_DATA/PROJECT_FORMAT.md`:

- `project.json` — strict manifest of immutable discovery metadata
  validated on every open (format id, format version, project UUID, display
  name, creation timestamp, database path, minimum application version,
  georeference summary). Unknown keys, unknown format ids, and unsupported
  versions are rejected without modification. The manifest carries no
  schema-version copy; the database is the single migration authority.
- `project.db` — SQLite database holding canonical structured state.
- `assets/`, `terrain/`, `scenarios/` — file-backed content locations.
- `cache/`, `autosave/`, `logs/` — non-canonical or session data.

Project names become directory names and are therefore restricted to a
portable charset (letters, digits, space, `-`, `_`); Windows reserved device
names are rejected.

## Schema v1

`schema_migrations` records forward-only migration ids. Migration 1
("core project foundation") creates:

- `project_state` — singleton row with project UUID, display name, traffic
  side (`left`/`right`), canonical `revision`, `saved_revision`, and
  created/modified timestamps.
- `georeference` — singleton row with the canonical horizontal CRS, linear
  unit, axis convention, project origin in the CRS, and optional vertical CRS.

Opening a database whose applied schema version is newer than the supported
version fails on a read-only probe; the project is not modified.

## Revision and dirty state

- `revision` is the canonical mutation counter. It is 1 at creation and
  increases with every accepted mutation (mutation commands arrive with later
  domains).
- `saved_revision` marks the revision last covered by an explicit save.
- A session is dirty while `revision != saved_revision`; dirty state is
  derived from persisted counters, so it survives reopen honestly.
- `project.save` is one SQLite transaction updating `saved_revision` and
  `modified_at`; the manifest is not rewritten, so a failed save cannot
  leave two files disagreeing. Saving a clean session is a no-op without
  events.

## Save-as semantics

`project.save_as` copies the open project to a new `<name>.iforge` directory
(the target must not exist), assigns a **fresh project UUID**, and switches
the session to the copy. The original project file remains untouched and
independently openable. The copy keeps content revision continuity but is a
new project identity; treat save-as as forking, not renaming.

## Integrity policy

On open, manifest and database must agree on project UUID, display name,
georeference, and `createdAt`. A mismatch is reported as corruption and
fails the open; the engine never silently repairs project files. The
schema-version probe runs on a read-only connection (`query_only`, no
journal-mode reconfiguration), so rejecting a newer project never modifies
the file — including its journal mode.

## Events

- `project.opened` — after create/open/save-as; carries the full summary.
- `project.closed` — after close or save-as switch; carries the project UUID.
- `project.revision_changed` — reserved for mutation commands.
- `project.dirty_state_changed` — when dirty state transitions.

Events carry an `event_id` and are broadcast to authenticated connections.
They are projections of engine-side facts, never instructions or state
transfers.

## Security notes

Connections join the event router only after ClientHello, protocol
negotiation, and token validation succeed, and only after the ServerHello
was delivered. Unauthenticated loopback peers receive no project events.

## Current limitations

- Autosave, autosave recovery, and explicit recovery tooling are later
  milestones. Creation and save-as write the manifest before switching
  sessions and roll back the whole target directory on failure; an ordinary
  save touches only the database.
- Concurrent open of the same project by two engine processes relies on
  SQLite locking; a dedicated project lockfile arrives with packaging and
  recovery hardening (issue #16).
- Graceful project-aware engine shutdown (see `docs/01_ARCHITECTURE/PROCESS_MODEL.md`)
  is not implemented; the desktop supervisor still terminates the child at
  quit, and the store flushes the SQLite session on engine exit.
- Undo/redo applies to user-facing edit mutations and is therefore not part
  of the lifecycle commands.
