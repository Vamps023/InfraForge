# Technical Requirements Document — InfraForge

## 1. Architecture objective

InfraForge uses strict process and dependency boundaries so frontend presentation, domain logic, persistence, simulation, and rendering can evolve independently without duplicated canonical state.

## 2. Primary technology decisions

- Native engine/domain runtime: C++23.
- Native renderer: Vulkan.
- Desktop shell: Electron.
- Frontend: React + TypeScript.
- Frontend/backend transport: versioned WebSocket over loopback for local desktop operation.
- Contract schema: Protocol Buffers, with generated C++ and TypeScript bindings.
- Structured project database: SQLite.
- Large binary/geospatial/assets: file-backed project content, referenced by canonical records.
- Project geospatial transforms: centralized geo service; consumers may not implement independent CRS/origin logic.

These are approved architecture decisions, not claims that implementations already exist.

## 3. Layer rules

### Frontend

Owns presentation, panel layout, active tool, selection presentation, camera presentation preferences, form draft state, and operation visualization. It must not own canonical project entities.

### Desktop shell

Owns OS window/process lifecycle, native viewport hosting, engine launch/termination, validated IPC needed for OS integration, file dialogs, and updater integration. It must not implement project/domain behavior.

### Transport

Owns framing, authentication handshake, version negotiation, request correlation, cancellation routing, and event delivery. It must not contain domain decisions.

### Application

Owns commands/use cases, transaction boundaries, authorization/session context for commands, undo command recording, and orchestration between domain services and adapters.

### Domain

Owns canonical entities, invariants, topology, semantic validation, and pure computations. It has no React, Electron, SQLite, or Vulkan dependency.

### Infrastructure adapters

Own persistence, filesystem, geospatial libraries, format adapters, logging sinks, and platform services behind interfaces defined inward.

### Renderer

Consumes immutable/derived render-scene updates. Renderer data cannot be written back as canonical domain state except through explicit user/tool commands carrying semantic data.

## 4. Dependency direction

`UI -> generated contract client -> WebSocket -> application -> domain`

Infrastructure adapters implement interfaces required by application/domain. Renderer receives domain-derived scene deltas through a renderer-facing scene service.

Forbidden dependencies include domain -> Electron, domain -> React, domain -> Vulkan, renderer -> SQLite queries, frontend -> native domain implementation, and feature UI -> another feature's internal files.

## 5. Canonical identifiers

Persisted entities use stable 128-bit UUID-compatible identifiers. Human-readable names are metadata and are not identity. Import adapters maintain source identifiers separately.

## 6. Revisions and concurrency

Every accepted project mutation produces a monotonically increasing project revision. Mutating requests include the client's last known revision when conflict detection matters. The application layer rejects stale destructive operations rather than silently overwriting newer state.

## 7. Error model

Every failed command returns a machine-readable error code, human-readable message, request ID, and optional structured details. Internal exception text and stack traces are logged but not blindly exposed to UI.

## 8. Long operations

Imports, exports, terrain processing, asset processing, and large rebuilds execute as cancellable jobs when cancellation can preserve consistency. Job state is `queued`, `running`, `completed`, `failed`, or `cancelled`. Completion is emitted only after transactional side effects succeed.

## 9. Persistence

Canonical structured data is stored transactionally. Generated cache, mesh, and GPU data is rebuildable. Project migration is schema-versioned. Unknown newer schema versions fail open attempts explicitly rather than being modified.

## 10. Rendering

Global domain coordinates use double precision. GPU-facing coordinates use camera-relative/local representations appropriate for precision. World partitioning limits resident scene data. Dirty tracking distinguishes geometry, material, topology, terrain, and simulation invalidations.

2D and 3D are view adapters over the same canonical domain and application command path. They may have independent camera and input controllers, but they must share entity identifiers, snapping/constraint services, selection resolution, diagnostics, revisions, undo records, and invalidation results. A view must not introduce a second road, lane, junction, CRS, or mesh-authoring model. Quality parity is verified with paired 2D/3D acceptance tests for every authoring capability.

## 11. Security

The local engine binds only to loopback by default. Desktop launch supplies a cryptographically random session token. The WebSocket connection must authenticate during handshake before command processing. Electron renderer context has no direct Node access; preload exposes narrow validated APIs.

## 12. Observability

Each process emits structured logs containing timestamp, severity, process/component, request/job ID where applicable, and message. User-facing Problems/Operations views consume normalized diagnostic/job events rather than scraping log text.

## 13. Linear-infrastructure geometry conditioning

External road/rail linework is treated as imported source evidence, not as canonical engineering geometry by default. Import adapters retain source coordinates, source IDs/tags, and provenance separately from canonical entities.

All smoothing, source-deviation checks, radius evaluation, and line/arc/clothoid fitting must occur after transformation through the centralized Geo service into canonical project coordinates. Algorithms must not apply engineering tolerances directly in WGS84 latitude/longitude degrees.

The application/domain pipeline must distinguish:

- retained source geometry/provenance;
- rebuildable conditioned working geometry;
- persisted canonical mathematical alignment;
- rebuildable renderer tessellation.

Topology-defining anchors such as true road junctions, rail switches/turnouts, branch points, user-pinned controls, and semantically fixed attachments cannot be displaced by a smoothing pass. Artificial source-way splits may be joined only when topology and source semantics prove that the join is safe.

Visual smoothing algorithms such as Chaikin corner cutting may be used for preview/inspection, but preview splines/polylines are never the canonical road/rail model. Engineering alignment uses deterministic domain services and line/circular-arc/clothoid primitives where supported. Import/fitting failures that exceed configured deviation or continuity constraints produce typed diagnostics rather than silently forcing a visually smooth result.

Chunk boundaries are not geometry boundaries. Derived tessellation may change density by LOD but must sample the same canonical curve and remain deterministic/seam-safe across adjacent chunks.

See `docs/05_DOMAINS/LINEAR_INFRASTRUCTURE_GEOMETRY.md` for the detailed cross-domain contract and verification fixtures.
