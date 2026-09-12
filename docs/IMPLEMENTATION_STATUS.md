# InfraForge implementation status

This file is the authoritative high-level implementation ledger. Design documents describe required behavior; they do not prove implementation.

## Status vocabulary

- **Implemented**: production code exists and is reachable through its real entry point.
- **Integrated**: implemented across all required process/domain boundaries.
- **Verified**: the relevant automated/manual verification has been executed and recorded.
- **Not implemented**: design is approved but production code does not exist yet.

## Current state

| Area | Status | Evidence |
|---|---|---|
| Repository initialization | Implemented | `README.md`, repository conventions, source-of-truth docs |
| Product requirements | Implemented as specification | `docs/00_PRODUCT/PRD.md` |
| Technical architecture | Implemented as specification | `docs/01_ARCHITECTURE/TRD.md` |
| Accepted architecture decisions | Implemented as specification | `docs/ADR/` |
| Native C++ executable baseline | Verified | `engine/src/main.cpp`; GitHub Actions native build/self-check succeeded on foundation commit `2efa77aa6a8300b8d798573f64b7eeacbd4873c3` |
| Native dependency manifest | Verified | `vcpkg.json` (ixwebsocket, protobuf, sqlite3, nlohmann-json, doctest, proj) pinned to immutable registry baseline; full native build passed on Windows and Linux in the native workflow for commit `28d46b877c8cc30759e9f653876c3afa226c8056`; PROJ 9.8.1 added for the georeference runtime (verified in the local Release build) |
| Protocol Buffer foundation schema | Verified | `contracts/proto/infraforge/protocol/v1/foundation.proto` + `project.proto`; Buf lint and TypeScript generation pass in CI |
| TypeScript protocol generation | Verified | Buf generation + strict TypeScript + production frontend build passed in CI (web/desktop workflow) |
| Authenticated loopback WebSocket server | Verified | `engine/src/network/WebSocketServer.cpp`; token/protocol handshake exercised end-to-end by `tools/engine-smoke/smoke.ts` against the real engine process on Windows and Linux; connections register for event delivery only after successful authentication (`engine/tests/WebSocketServerTests.cpp`) |
| Project lifecycle (create/open/save/save-as/close/summary) | Verified | `engine/src/application/ProjectService.cpp`, `engine/src/persistence/SqliteProjectStore.cpp`; SQLite create → close → reopen with identical canonical metadata verified end-to-end by `npm run verify:engine` (real engine, authenticated WebSocket, no mocks) and by `infraforge-engine --self-check`; doctest suite `infraforge-engine-tests` covers store/service/processor semantics including newer-schema rejection without modification, journal-mode preservation on rejected opens, migration-then-reopen continuity, save atomicity (manifest untouched), and corrupt-database reporting as `COMMAND_ERROR_CODE_PERSISTENCE_FAILURE`; event delivery is restricted to authenticated connections with an in-process two-client isolation test (`engine/tests/WebSocketServerTests.cpp`) |
| Project format (`.iforge` + `project.json` + `project.db`) | Verified | `engine/src/persistence/ProjectManifest.cpp`, `engine/src/persistence/SchemaMigrations.cpp`; strict manifest validation, forward-only migrations, schema v1 (`project_state`, `georeference`) + migration 2 (`origin_height`) covered by persistence tests including v1→v2 upgrade |
| Project domain documentation | Implemented as specification | `docs/05_DOMAINS/PROJECT.md` |
| Electron desktop shell | Build verified | secure `main.ts`/`preload.ts`; desktop TypeScript/build passed in CI |
| Desktop engine supervision | Build verified; runtime integration pending native runtime | `apps/desktop/src/EngineSupervisor.ts`; random token, bounded bind retries, readiness validation |
| Desktop OS directory dialogs | Build verified | `apps/desktop/src/main.ts` `dialog:pick-directory`, narrow preload API `apps/desktop/src/preload.ts` |
| React editor shell | Build verified | `apps/frontend/src/App.tsx`, `styles.css`, UI-only Zustand stores; strict TypeScript and production build passed in CI |
| Frontend authenticated engine session | Build verified; end-to-end runtime pending desktop runtime | `apps/frontend/src/lib/engineSession.ts`; command correlation + event subscription over the generated protocol; the frontend declares its own generated protocol version rather than echoing the engine's advertised one |
| Frontend project lifecycle actions | Build verified; end-to-end runtime pending desktop runtime | `apps/frontend/src/features/project/` — New Project dialog, Open/Save/Close actions wired to real engine commands; projections from backend results/events only |
| Engine protocol smoke verification | Verified | `tools/engine-smoke/smoke.ts`; `npm run verify:engine` launches the real engine and exercises hello → create → summary → save → close → reopen with event-stream checks; passed on Windows (repeated local runs) and Linux in the native workflow for commit `28d46b877c8cc30759e9f653876c3afa226c8056` |
| Web/desktop CI | Verified | protocol lint, strict TypeScript, frontend build, and desktop build succeeded on GitHub Actions for commit `28d46b877c8cc30759e9f653876c3afa226c8056` |
| Graceful project-aware engine shutdown | Not implemented | No project transaction lifecycle exists yet; current supervisor terminates child on app quit; engine flushes SQLite session at exit. Tracked in `docs/05_DOMAINS/PROJECT.md` limitations |
| Canonical georeference runtime | Geo runtime verified; renderer boundary API implemented; renderer consumption pending integration | GitHub issue #3. `engine/src/domain/geo/` (PROJ-backed `GeoTransformService`, canonical `GeoreferenceConfig`, `RenderLocalFrame` boundary API), `application::GeoService`, `geo.*` commands + `georeference_changed` event (proto 1.2), schema migration 2 (`origin_height`), `ProjectStore::updateGeoreference`, frontend `features/geo/` projection + settings panel. Verified by `GeoTransformServiceTests`/`GeoPersistenceTests`/`GeoServiceTests`/`CommandProcessorTests`, `infraforge-engine --self-check` control point, and `npm run verify:engine` (real engine: query → transform control point → set → save → reopen persistence, event stream). Renderer consumption lands with the viewport domain (issue #2) |
| Native viewport hosting (child surface) | Implemented; runtime verified on Windows | `viewport/` module (`infraforge-viewport` process): real child HWND parented into the shell window, stdio control protocol (place/visibility/shutdown), placement from page rect + window content bounds converted with `screen.dipToScreenRect` (per-display DIP→physical on mixed-DPI setups), Chromium re-parent self-healing, per-monitor-v2 DPI awareness enabled before window creation (doctest-guarded), class brush lifecycle registered/unregistered with the child window class. Supervisor startup-failure semantics (readiness timeout kills the spawned child, no orphaned process, restartable after failure) are covered by `apps/desktop/tests/ViewportSupervisor.test.ts` (real child processes via vitest, run in the web/desktop workflow). Verified live: surface is topmost exactly over the viewport host rect, survives maximize/restore/move and a monitor change, follows cross-monitor moves, clean shutdown. A live 100% ↔ 125/150% mixed-DPI monitor change under the Electron shell remains an outstanding manual acceptance step |
| Vulkan renderer core + grid pass | Implemented; verified | `viewport/src/renderer/`: Vulkan 1.3 instance/device/surface/swapchain with RAII ownership, swapchain lifecycle state machine with explicit acquire-result handling (out-of-date recreates before retry, suboptimal acquire renders and presents the acquired image before recreating so the acquire semaphore is consumed, timeout/not-ready skip the frame, surface-lost/memory/device-lost fail the renderer explicitly, zero extent suspends), render thread with guaranteed join-after-self-stop (`RenderThread`), embedded-GLSL grid pass (shaderc) with RAII staging upload, orthographic grid camera, selection-ID registry contract. Verified live on NVIDIA GeForce RTX 3060: world grid renders in the editor viewport; maximize/restore/monitor-change/clean-shutdown run with KHONOS validation layers enabled and zero validation findings. 32 doctest cases cover the acquire/present decision table, surface format/colorspace pair selection, render-thread join discipline, selection ids, and camera math (`infraforge-viewport-tests`); native (Windows and Ubuntu, build + doctest suite + engine self-check + `verify:engine`) and web/desktop workflows green on the rebased branch for commit ed94d2ba9faef51a3767070c2e9abf191ce22eb7 |
| Renderer status projection | Implemented; runtime verified on Windows | Starting/ready/suspended/recreating/device_lost/failed/stated records flow viewport → shell → React status bar with GPU name and Vulkan version (`Renderer ready · NVIDIA GeForce RTX 3060` observed live); visibility control commands re-assert the renderer's last explicit record instead of ever implying readiness for a dead renderer (doctest-covered policy, `visibilityStatusReport`); validation mode via `INFRAFORGE_VIEWPORT_VALIDATE=1` (requires the KHONOS validation layer to be provisioned locally, e.g. from the Vulkan SDK or vcpkg's `vulkan-validationlayers`; it is deliberately not a build dependency) |
| World partition runtime | Not implemented | GitHub issue #4; design only |
| Terrain | Not implemented | GitHub issue #6; design only |
| Roads/lanes/junctions | Not implemented | GitHub issues #7 and #8; design only |
| Traffic infrastructure | Not implemented | GitHub issue #11; design only |
| Traffic simulation | Not implemented | GitHub issue #12; design only |
| Rail | Not implemented | GitHub issue #13; design only |
| World validation | Not implemented | GitHub issue #14; design only |
| Export/interoperability | Not implemented | GitHub issue #15; design only |
| Packaging/security/recovery | Not implemented | GitHub issue #16; design only |

## Verification rule

No entry may be promoted to **Implemented**, **Integrated**, or **Verified** without concrete source paths and verification evidence. The native protocol/project rows were promoted after the Windows and Linux native workflow (build, doctest suite, self-check, engine protocol smoke) and the web/desktop workflow passed for commit `28d46b877c8cc30759e9f653876c3afa226c8056`.
