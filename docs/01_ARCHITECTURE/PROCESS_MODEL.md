# Process model

## Processes

### 1. Desktop shell (`InfraForge`)

Responsibilities:

- create/manage desktop windows;
- launch and supervise `infraforge-engine`;
- create the per-session authentication token;
- pass the token to the engine through process arguments/environment without storing it in project data;
- parse the engine readiness record;
- expose the engine endpoint to the renderer through a narrow preload API;
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

1. Electron generates a session token.
2. Electron launches `infraforge-engine --host 127.0.0.1 --port 0 --session-token <token>`.
3. Engine binds to loopback and an OS-selected free port.
4. Engine emits one machine-readable readiness record containing protocol version and bound port.
5. Electron validates the readiness record and exposes `{host, port, token, protocolVersion}` through preload.
6. Frontend opens WebSocket.
7. Frontend sends authentication/hello message.
8. Engine accepts commands only after token and protocol compatibility validation.

If any step fails, the UI displays the engine state and actionable failure. There is no browser-only fallback that pretends native functionality is available.

## Shutdown sequence

1. Frontend requests project close if needed.
2. Engine rejects shutdown while a non-interruptible transaction is committing; otherwise jobs are cancelled/settled according to job policy.
3. Persistence is flushed/checkpointed.
4. WebSocket closes cleanly.
5. Renderer resources are destroyed.
6. Engine exits.
7. Electron force-terminates only after the supervised graceful path fails.