# Process model

## Processes

### 1. Desktop shell (`InfraForge`)

Responsibilities:

- create/manage desktop windows;
- launch and supervise `infraforge-engine`;
- create the per-session authentication token;
- select a loopback port candidate and retry startup if another process wins the bind race;
- pass the token to the engine through process arguments without storing it in project data;
- parse and validate the engine readiness record;
- expose the authenticated engine endpoint to the renderer through a narrow preload API;
- host/manage the native Vulkan viewport surface;
- perform user-authorized OS file/directory dialogs;
- terminate child processes during shutdown.

The desktop shell must not parse road files, mutate project entities, run simulation, or contain business rules.

### 2. Frontend renderer process

Responsibilities:

- render UI;
- establish the authenticated WebSocket session;
- maintain query/projection caches;
- present operations/diagnostics;
- convert user intent into typed commands;
- maintain transient UI state.

Node integration is disabled in the renderer.

### 3. Native engine process (`infraforge-engine`)

Responsibilities:

- canonical project session;
- WebSocket endpoint;
- commands/events;
- persistence;
- geospatial services;
- authoring domain services;
- import/export jobs;
- simulation;
- render-scene generation/invalidation.

### 4. Native renderer execution

Renderer ownership may live inside the engine process or a dedicated native viewport module, but its public boundary remains render-scene commands/data rather than domain object mutation. The embedding choice must be proven against Windows resize/DPI/focus/multi-monitor/destruction behavior before road authoring begins.

## Local startup sequence

1. Electron generates a 256-bit random session token.
2. Electron asks the OS for a currently free loopback port candidate, releases the probe socket, and launches `infraforge-engine --host 127.0.0.1 --port <candidate> --session-token <token>`.
3. Engine binds only to `127.0.0.1` and fails explicitly if the candidate is no longer available.
4. If bind/startup fails, Electron chooses a new candidate and retries a bounded number of times; it never silently connects to an unrelated listener.
5. Engine emits one machine-readable readiness record containing protocol version and the exact bound endpoint.
6. Electron validates the readiness record against the requested endpoint and exposes `{host, port, token, protocolVersion}` through preload.
7. Frontend opens WebSocket and sends `ClientHello` as a binary Protocol Buffer frame.
8. Engine accepts application messages only after token and protocol compatibility validation.

If any step fails, the UI displays the engine state and actionable failure. There is no browser-only fallback that pretends native functionality is available.

## Shutdown sequence

1. Frontend requests project close if needed.
2. Engine rejects shutdown while a non-interruptible transaction is committing; otherwise jobs are cancelled/settled according to job policy.
3. Persistence is flushed/checkpointed.
4. WebSocket closes cleanly.
5. Renderer resources are destroyed.
6. Engine exits.
7. Electron force-terminates only after the supervised graceful path fails.

The graceful project-aware shutdown protocol is not implemented in the foundation transport milestone; until project persistence exists, Electron supervision terminates the child engine during application quit and the implementation ledger must state that limitation.