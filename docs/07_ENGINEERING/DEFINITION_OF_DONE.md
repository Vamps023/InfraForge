# Definition of Done

A production feature is complete only when all applicable layers are complete.

## Required trace

1. Product requirement/acceptance behavior exists.
2. Reachable UI or public API entry point exists.
3. Typed contract exists when crossing the process boundary.
4. Application command/query handler exists.
5. Domain logic/invariants exist.
6. Persistence exists when the feature is project-owned.
7. Undo/redo exists for reversible editor mutations.
8. Render invalidation/scene update exists when visually represented.
9. Errors/diagnostics are surfaced.
10. Long-running work has real job/progress/cancellation behavior where applicable.
11. Automated verification covers logic and integration boundaries.
12. Build/type/format checks pass.
13. User-visible/documented behavior is updated.
14. `docs/IMPLEMENTATION_STATUS.md` is updated with concrete evidence.

## Explicitly not sufficient

- a class or interface exists;
- a UI button exists without production path;
- a unit test exercises an otherwise unreachable helper;
- an IPC/WebSocket route returns hard-coded success;
- mocked renderer output is presented as implementation;
- a schema is documented but not used by production code;
- compilation alone is presented as runtime verification.

## Review evidence

Feature handoff states exact production files/functions, entry point, persisted data affected, protocol messages, verification commands/results, and remaining limitations. Unknown or untested behavior is reported as such.