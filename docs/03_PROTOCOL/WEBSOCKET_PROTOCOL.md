# WebSocket protocol

## Transport

Local desktop communication uses WebSocket over loopback. Production local mode binds to `127.0.0.1`; non-loopback exposure is not enabled implicitly.

Payload schemas are defined in versioned Protocol Buffer files. JSON examples in documentation are explanatory only and are not a second wire contract.

## Connection authentication

Electron generates a cryptographically random session token and passes it to the engine at launch. The frontend obtains the token through the secure preload API. The first application message is a `ClientHello` containing protocol version and session token. The engine sends `ServerHello` only after validation.

Unauthenticated connections cannot invoke commands or subscribe to project data.

## Message classes

- `hello` — authentication and version negotiation.
- `command` — user/application intent that may mutate/query canonical state.
- `result` — correlated success or failure for a command.
- `event` — server-originated state change/diagnostic/job notification.
- `cancel` — cancellation request for a cancellable command/job.
- `ping/pong` — connection health.

## Correlation

Each command has a unique request ID. Results echo that ID. Long-running operations return a job ID once accepted; lifecycle events reference that job ID.

## Revision-aware mutation

Mutating commands that depend on an observed project state include `expected_project_revision`. The application layer may reject stale operations with a conflict error containing the current revision.

## Error envelope

Failures include:

- stable error code;
- user-safe message;
- request ID;
- optional current project revision;
- optional structured field/entity details.

Internal stack traces are logs, not protocol messages.

## Progress

Progress events are based on measurable work units when available. A workflow with unknown completion fraction reports indeterminate running state rather than fabricated percentages.

## Compatibility

Protocol major mismatch rejects the session. Backward-compatible additions increment minor/schema metadata according to the contract-generation policy. Generated TypeScript/C++ bindings must be produced from the same committed schema revision.