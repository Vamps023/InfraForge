# Event model

Events are server-originated facts, not instructions.

## Foundation events

- `engine.status_changed`
- `project.opened`
- `project.closed`
- `project.revision_changed`
- `project.dirty_state_changed`
- `job.queued`
- `job.started`
- `job.progress`
- `job.completed`
- `job.failed`
- `job.cancelled`
- `diagnostic.added`
- `diagnostic.removed`
- `diagnostic.cleared`

Domain events are added with their domain implementation and must contain stable entity IDs plus project revision when they describe canonical state changes.

## Event delivery

The frontend treats events as cache/projection invalidation or incremental projection updates. Events do not transfer ownership of canonical mutable objects to the frontend.