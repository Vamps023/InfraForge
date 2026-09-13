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

## Planned events (WIP / future milestone implementation)

These event families are part of the target architecture but remain Work In Progress (WIP) to be introduced alongside their production domain implementations:

### Job lifecycle (Phase 8+)
- `job.queued`
- `job.started`
- `job.progress`
- `job.completed`
- `job.failed`
- `job.cancelled`

### Diagnostics (Phase 8+)
- `diagnostic.added`
- `diagnostic.removed`
- `diagnostic.cleared`

### Domain mutations (Phases 6–13)
- Terrain, road geometry, lane connectivity, and traffic simulation invalidation events will be added strictly with their respective domain logic.

## Event delivery rules

1. The frontend treats events as triggers for cache/projection invalidation or incremental display updates.
2. Events never convey mutable ownership to the client.
3. Domain events that describe persistent entity mutations must include stable entity IDs and the resulting canonical `project_revision`.