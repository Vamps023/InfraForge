# Validation domain

The Validation domain owns world validation: the `world.check` command that
inspects canonical project state and produces structured diagnostics. It is
the foundation for all future domain-specific validators.

## Production path

```text
Frontend action (Check World button)
    → generated protobuf command (contracts/proto/infraforge/protocol/v1/validation.proto)
    → WebSocket (authenticated loopback session)
    → application executor (ValidationService, single-threaded)
    → validator registry (deterministic ordering)
    → canonical project state snapshot (ProjectStore)
    → structured diagnostic result frame
    → frontend validation projection (Problems panel)
```

The frontend never computes diagnostics. It sends the `world.check` command
and projects the structured result.

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

## Validator architecture

### Validator interface

Every validator implements `Validator`:

- `name()` — returns a stable, non-empty identifier used as the `source` field.
- `validate(context, cancellation, output)` — inspects the context and appends
  diagnostics to `output`. Must be deterministic for a given context. Must not
  throw; internal failures are reported as diagnostics. Must poll the
  cancellation token at safe points.

### Validation context

`ValidationContext` is an immutable snapshot of the canonical project state
captured before any validator runs. Validators consume this snapshot, never
the live store, so diagnostics are deterministic against a fixed revision.

### Validator registry

`ValidatorRegistry` owns its validators and executes them in registration
order. Registration happens once at engine startup; the registry is not
modified at runtime. This keeps validation runs deterministic and
reproducible.

### Cancellation

`CancellationToken` provides a cooperative cancellation boundary. The
validation service sets the flag when cancellation is requested; validators
poll `isCancelled()` at safe points and return early. Partial results are
discarded when cancelled; the result carries `cancelled=true`.

## Application layer

`ValidationService` is the use case for `world.check`:

- Requires an open project; throws `ProjectNotOpen` when none is open.
- Captures a revision snapshot before running validators.
- Runs all registered validators in order.
- Returns a `ValidationResult` with diagnostics, revision, and cancelled flag.
- Validator internal errors are reported as diagnostics, not exceptions.

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

## Protocol

`contracts/proto/infraforge/protocol/v1/validation.proto` defines:

- `WorldCheckCommand` — request a validation run.
- `WorldCheckResult` — diagnostics + revision + cancelled flag.
- `Diagnostic` — structured diagnostic message.
- `DiagnosticSeverity` — Info/Warning/Error enum.
- `DiagnosticAddedEvent` / `DiagnosticRemovedEvent` / `DiagnosticClearedEvent`
  — incremental diagnostic events for future revision-aware validation.

The `world.check` command is wired into `CommandEnvelope`, `WorldCheckResult`
into `ResultEnvelope`, and diagnostic events into `EventEnvelope`.

## Frontend projection

The frontend receives structured diagnostics from `world.check` results and
projects them into the Problems panel:

- `validationStore` — UI-only Zustand store holding diagnostics, revision,
  and checking state.
- `validationApi` — sends the `world.check` command and projects the result.
- `validationEvents` — subscribes to diagnostic events for incremental updates.

The Problems panel displays structured diagnostics with severity, code,
message, and source. A "Check World" button in the toolbar triggers
validation when a project is open.

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
