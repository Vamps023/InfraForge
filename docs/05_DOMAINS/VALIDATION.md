# Validation domain

The Validation domain owns world validation: the `world.check` command that
inspects canonical project state and produces structured diagnostics with
stable entity references. It is the foundation for all future domain-specific
validators.

## Production path

```text
Frontend action (Check World button)
    → generated protobuf command (contracts/proto/infraforge/protocol/v1/validation.proto)
    → WebSocket (authenticated loopback session)
    → application executor (ValidationService, single-threaded)
    → validator registry (deterministic ordering)
    → canonical project state snapshot (ProjectStore)
    → structured diagnostic result frame
    → engine-owned DiagnosticStore diffs against previous set
    → diagnostic added/removed/cleared events broadcast to all connections
    → frontend validation projection (Problems panel)
```

The frontend never computes diagnostics. It sends the `world.check` command,
receives the structured result, and projects incremental events from the
engine-owned diagnostic store.

## Canonical diagnostic model

Every diagnostic is a structured, stable, deterministic record:

- **code** — stable machine-readable identifier (e.g.
  `project.foundation.georeference.horizontal_crs_empty`). Never parsed from
  human-readable text; never changes for the same condition.
- **severity** — `Info`, `Warning`, or `Error`, ordered least to most severe.
- **source** — the validator that produced the diagnostic (e.g.
  `project.foundation`).
- **message** — human-readable description; may change between engine versions
  and is never machine-parsed.
- **entities** — zero or more stable canonical entity references (kind + UUID).
- **revision** — the project revision the diagnostic was computed against.
- **suggestedAction** — optional remediation hint with a stable `kind` tag and
  human-readable `label`.

### Diagnostic identity

A diagnostic `code` alone is not a unique instance. Multiple entities may
produce the same code (e.g. `road.geometry.self_intersection` on two roads).
Identity is the stable tuple (code, entities), computed as a deterministic
string by `Diagnostic::diagnosticIdentity()`. The message is never part of
identity. Entities are sorted by (kind, id) so identity is stable regardless
of the order a validator emits them.

The `DiagnosticRemovedEvent` carries both `code` and `entities` so multi-entity
diagnostics with the same code can be removed individually.

## Validator architecture

### Validator interface

Every validator implements `Validator`:

- `name()` — returns a stable, non-empty identifier used as the `source` field.
  The registry rejects empty names and duplicate names at registration.
- `validate(context, cancellation, output)` — inspects the context and appends
  diagnostics to `output`. Must be deterministic for a given context. Must not
  throw; internal failures are reported as diagnostics. Must poll the
  cancellation token at safe points and return early when cancelled.

### Validation context

`ValidationContext` is an immutable snapshot of the canonical project state
captured before any validator runs. Validators consume this snapshot, never
the live store, so diagnostics are deterministic against a fixed revision.

### Validator registry

`ValidatorRegistry` owns its validators and executes them in registration
order. Registration happens once at engine startup; the registry is not
modified at runtime. The registry rejects:
- null validators
- validators with empty names
- duplicate validator names

This keeps validation runs deterministic and reproducible.

### Cancellation

`CancellationToken` provides a cooperative cancellation boundary. The
`CommandProcessor` creates a `shared_ptr<CancellationToken>` for each
validation run and stores it in `activeCancellation_`. The
`world.cancel_check` command is intercepted in `post()` on the network thread
(not queued on the executor) and calls `requestCancellation()` on the active
token. Validators poll `isCancelled()` at safe points and return early.

**Partial-result policy:** when cancelled, the service discards all partial
results and returns `cancelled=true` with empty diagnostics. Partial
diagnostics are never published as authoritative results. The
`DiagnosticStore` is not updated, so the previous published set remains valid.

## Application layer

`ValidationService` is the use case for `world.check`:

- Requires an open project; throws `ProjectNotOpen` when none is open.
- Captures a revision snapshot before running validators.
- Runs all registered validators in order.
- Returns a `ValidationResult` with diagnostics, revision, and cancelled flag.
- Validator internal errors are reported as diagnostics, not exceptions.
- When cancelled, returns empty diagnostics with `cancelled=true`.

## Engine-owned diagnostic lifecycle

`DiagnosticStore` is the engine-owned canonical store of the currently
published diagnostic set. The `CommandProcessor` owns one instance.

After each completed (non-cancelled) validation run:
1. The new diagnostic set is compared against the previously published set
   using `DiagnosticStore::publish()`.
2. Added diagnostics (in the new set but not the old) are broadcast as
   `DiagnosticAddedEvent`.
3. Removed diagnostics (in the old set but not the new) are broadcast as
   `DiagnosticRemovedEvent` with (code, entities) identity.
4. The store replaces its published set with the new set.

### Revision invalidation

When the canonical project state changes in a way that makes published
diagnostics stale, the `CommandProcessor` clears the `DiagnosticStore` and
broadcasts a `DiagnosticClearedEvent`:

- **Project closed** — `reason="project_closed"`. Diagnostics belong to the
  closed project and are invalid.
- **Revision changed** — `reason="revision_changed"`. The canonical revision
  changed; previously published diagnostics are stale.

The `revision_changed` path is architecturally wired in `publishEvents` but
is not triggered by any current mutation path on main (save on a non-dirty
project does not change the revision). It will fire when domain mutations
that change the revision are implemented.

The `WorldCheckResult` also carries a `stale` flag, set when the published
diagnostics were computed against a different revision than the current
canonical revision. The frontend uses this to prompt the user to re-run
Check World.

## Protocol

`contracts/proto/infraforge/protocol/v1/validation.proto` defines:

- `WorldCheckCommand` — request a validation run.
- `WorldCheckResult` — diagnostics + revision + cancelled + stale flags.
- `WorldCancelCheckCommand` — cancel an in-progress validation run.
- `WorldCancelCheckResult` — acknowledgment with `cancellation_requested`.
- `Diagnostic` — structured diagnostic message.
- `DiagnosticSeverity` — Info/Warning/Error enum.
- `DiagnosticAddedEvent` — a diagnostic was added to the published set.
- `DiagnosticRemovedEvent` — a specific diagnostic (code + entities) was
  removed from the published set.
- `DiagnosticClearedEvent` — all diagnostics were cleared (project closed or
  revision changed), with a `reason` field.

The `world.check` and `world.cancel_check` commands are wired into
`CommandEnvelope`. `WorldCheckResult` and `WorldCancelCheckResult` are wired
into `ResultEnvelope`. Diagnostic events are wired into `EventEnvelope`.

## Frontend projection

The frontend receives structured diagnostics from `world.check` results and
projects them into the Problems panel:

- `validationStore` — UI-only Zustand store holding diagnostics, revision,
  cancelled, stale, and checking state. Uses identity-based add/remove to
  prevent duplicates.
- `validationApi` — sends `world.check` and `world.cancel_check` commands and
  projects results into the store.
- `validationEvents` — subscribes to diagnostic events for incremental
  updates from the engine-owned lifecycle.

The Problems panel displays:
- "Checking world…" while a run is in flight.
- "Validation was cancelled. Re-run Check World…" when cancelled.
- "Diagnostics are stale (revision changed). Re-run Check World." when stale.
- Structured diagnostics with severity, code, message, and source.
- "No diagnostics (revision N)." when a valid project has zero diagnostics.
- "No diagnostics." when no run has completed.

A "Check World" button in the toolbar triggers validation when a project is
open.

### Entity interaction (not yet implemented)

Issue #14 acceptance requires "UI can select/frame supported problem
entities." The current architecture has no frameable entity type because the
renderer (Issue #2) and domain entities (roads, terrain, etc.) are not
merged. The Problems panel is architecturally ready for entity activation:
each diagnostic carries stable `entities` references. When the renderer and
domain layers merge, the frontend can dispatch entity selection/framing
through the canonical selection infrastructure. This is intentionally not
implemented with a fake viewport selection system.

## Initial validator

### ProjectFoundationValidator

Checks canonical state available on current main:

- `georeference.horizontal_crs` is non-empty.
- `georeference.linear_unit` is non-empty.
- `georeference.origin` is finite (not NaN/Inf).
- `revision >= 1`.
- `saved_revision <= revision`.

Each check produces a diagnostic with a stable code prefixed by
`project.foundation.` when the condition is violated. A genuinely valid
project produces zero diagnostics — this is not a hard-coded success response.

## Deferred validators

The following validators are intentionally deferred until their owning
domains merge:

- **Georeference runtime validation** (Issue #3) — depends on the canonical
  georeference runtime not yet merged.
- **Road/lane/junction validation** (Issues #7/#8) — road domain not merged.
- **Terrain validation** (Issue #6) — terrain domain not merged.
- **Traffic infrastructure validation** (Issue #11) — traffic domain not merged.
- **Rail validation** (Issue #13) — rail domain not merged.

The `ProjectFoundationValidator` checks only state that is already canonical
on current main; it does not depend on unmerged APIs.
