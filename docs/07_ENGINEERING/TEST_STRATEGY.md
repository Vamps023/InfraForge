# Test strategy

InfraForge verifies behavior at the lowest useful layer plus critical production paths.

## Test layers

### Domain unit tests

Pure invariants and algorithms: geometry, topology, lane rules, profiles, coordinate math, validation.

### Application tests

Commands, transaction boundaries, revision behavior, undo/redo, stale-revision conflict handling, job state.

### Persistence tests

Schema creation, migrations, save/reopen, transaction rollback, autosave/recovery, foreign keys, project isolation.

### Protocol tests

Schema compatibility, authentication, malformed message rejection, request correlation, errors, cancellation, reconnect/session behavior.

### Renderer/native tests

Resource lifetime helpers, scene delta application, coordinate conversion, chunk residency logic, Vulkan validation-enabled smoke scenarios on supported hardware/CI where available.

### Desktop integration tests

Electron launch -> engine readiness -> authenticated WebSocket -> project lifecycle -> UI state. Native viewport lifecycle scenarios include resize/DPI/focus/destroy/recreate.

### Golden project acceptance

Small deterministic projects exercise complete vertical workflows and round-trip persistence/import/export. Golden fixtures are versioned and migrated like real projects.

## Performance regression

Measured workloads cover project open, spatial queries, one-entity edit, chunk rebuild, renderer working-set changes, and representative simulation loads. Performance targets are budgets; regressions require investigation rather than hidden threshold relaxation.

## Test honesty

A test using mocks proves only the mocked boundary contract. Runtime/GPU/packaging behavior is not claimed verified without executing the relevant runtime scenario.