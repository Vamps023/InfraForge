# Event model

Events are server-originated facts broadcast to authenticated sessions. They notify the frontend and other consumers of canonical state changes, job progression, or diagnostics without transferring mutable state ownership.

## Naming and contract mapping

- **Logical event name**: Documented using lowercase dot-separated notation (e.g., `project.opened`, `georeference.changed`).
- **Protobuf wire field**: Defined in `contracts/proto/` as `snake_case` fields within the `ServerMessage` oneof (e.g., `project_opened`, `georeference_changed`).

## Active foundation events (implemented & verified)

These events are implemented in the C++ engine and verified by integration and smoke tests:

- `project.opened` (`project_opened`): Broadcast after `project.create`, `project.open`, or `project.save_as`; carries the full project summary.
- `project.closed` (`project_closed`): Broadcast when an open project session closes; carries the project UUID.
- `project.revision_changed` (`project_revision_changed`): Broadcast upon canonical project mutations; carries the updated project revision number.
- `project.dirty_state_changed` (`project_dirty_state_changed`): Broadcast when a session transitions between clean and dirty (`revision != saved_revision`).
- `georeference.changed` (`georeference_changed`): Broadcast after `geo.set_georeference`; carries the resolved `GeoreferenceInfo` and the new project revision.

## Implemented events (protocol 1.3)

These event families are implemented and emitted by the canonical `CommandProcessor` execution path:

### Job lifecycle
- `job.queued` (`job_queued`): Emitted when a command enters the processor queue. Carries job ID, operation name, label, target ID, and cancellable flag.
- `job.started` (`job_started`): Emitted when command execution begins. Carries job ID.
- `job.progress` (`job_progress`): Emitted only when a command produces real progress. Carries job ID, optional progress (0–1), optional processed/total (uint64 as bigint), and optional message. Absent fields are distinguishable from zero via protobuf `optional` semantics.
- `job.completed` (`job_completed`): Emitted after successful command handling. Carries job ID.
- `job.failed` (`job_failed`): Emitted on command failure. Carries job ID, stable error code, and error message.
- `job.cancelled` (`job_cancelled`): Defined in the protocol but not emitted by the current engine because no cancellation command exists.

### Diagnostics
- `diagnostic.added` (`diagnostic_added`): Emitted when a canonical diagnostic condition is detected. Carries diagnostic ID, source, severity, message, and optional target ID.
- `diagnostic.removed` (`diagnostic_removed`): Emitted when a previously-reported diagnostic condition is resolved. Carries diagnostic ID.
- `diagnostic.cleared` (`diagnostic_cleared`): Emitted to clear diagnostics by source or all. Carries source (empty = clear all backend diagnostics; shell-owned diagnostics survive).

### Current diagnostic source
The vertical-reference georeference check is the first real diagnostic source: when a project is created or opened without a vertical CRS, a `diagnostic.added` event is emitted; when a vertical CRS is added via `set_georeference`, a `diagnostic.removed` event is emitted; when the project closes, a `diagnostic.cleared` event is emitted.

### Domain mutations (Phases 6–13)
- Terrain, road geometry, lane connectivity, and traffic simulation invalidation events will be added strictly with their respective domain logic.

## Event delivery rules

1. The frontend treats events as triggers for cache/projection invalidation or incremental display updates.
2. Events never convey mutable ownership to the client.
3. Domain events that describe persistent entity mutations must include stable entity IDs and the resulting canonical `project_revision`.