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
| Native dependency manifest | Verified on Windows, Linux pending CI | `vcpkg.json` (ixwebsocket, protobuf, sqlite3, nlohmann-json, doctest) pinned to immutable registry baseline; vcpkg install + full native build passed locally on Windows x64; Linux runs in the native workflow on push |
| Protocol Buffer foundation schema | Verified | `contracts/proto/infraforge/protocol/v1/foundation.proto` + `project.proto`; Buf lint and TypeScript generation pass in CI |
| TypeScript protocol generation | Verified | Buf generation + strict TypeScript + production frontend build passed in CI (web/desktop workflow) |
| Authenticated loopback WebSocket server | Verified on Windows, Linux pending CI | `engine/src/network/WebSocketServer.cpp`; token/protocol handshake exercised end-to-end by `tools/engine-smoke/smoke.ts` against the real engine process on Windows; Linux native workflow runs the same smoke on push |
| Project lifecycle (create/open/save/save-as/close/summary) | Verified | `engine/src/application/ProjectService.cpp`, `engine/src/persistence/SqliteProjectStore.cpp`; SQLite create → close → reopen with identical canonical metadata verified end-to-end by `npm run verify:engine` (real engine, authenticated WebSocket, no mocks) and by `infraforge-engine --self-check`; doctest suite `infraforge-engine-tests` covers store/service/processor semantics including newer-schema rejection without modification |
| Project format (`.iforge` + `project.json` + `project.db`) | Verified | `engine/src/persistence/ProjectManifest.cpp`, `engine/src/persistence/SchemaMigrations.cpp`; strict manifest validation, forward-only migrations, schema v1 (`project_state`, `georeference`) covered by persistence tests |
| Project domain documentation | Implemented as specification | `docs/05_DOMAINS/PROJECT.md` |
| Electron desktop shell | Build verified | secure `main.ts`/`preload.ts`; desktop TypeScript/build passed in CI |
| Desktop engine supervision | Build verified; runtime integration pending native runtime | `apps/desktop/src/EngineSupervisor.ts`; random token, bounded bind retries, readiness validation |
| Desktop OS directory dialogs | Build verified | `apps/desktop/src/main.ts` `dialog:pick-directory`, narrow preload API `apps/desktop/src/preload.ts` |
| React editor shell | Build verified | `apps/frontend/src/App.tsx`, `styles.css`, UI-only Zustand stores; strict TypeScript and production build passed in CI |
| Frontend authenticated engine session | Build verified; end-to-end runtime pending desktop runtime | `apps/frontend/src/lib/engineSession.ts`; command correlation + event subscription over the generated protocol |
| Frontend project lifecycle actions | Build verified; end-to-end runtime pending desktop runtime | `apps/frontend/src/features/project/` — New Project dialog, Open/Save/Close actions wired to real engine commands; projections from backend results/events only |
| Engine protocol smoke verification | Verified on Windows, Linux pending CI | `tools/engine-smoke/smoke.ts`; `npm run verify:engine` launches the real engine and exercises hello → create → summary → save → close → reopen with event-stream checks; passed repeatedly on Windows x64 locally; the native workflow runs the same step on both OS on push |
| Web/desktop CI | Verified | protocol lint, strict TypeScript, frontend build, and desktop build succeeded on GitHub Actions for the web contract commits |
| Graceful project-aware engine shutdown | Not implemented | No project transaction lifecycle exists yet; current supervisor terminates child on app quit; engine flushes SQLite session at exit. Tracked in `docs/05_DOMAINS/PROJECT.md` limitations |
| Vulkan renderer | Not implemented | GitHub issue #2; no native renderer yet |
| Canonical georeference runtime | Not implemented | GitHub issue #3; design only. Creation-time canonical georeference storage exists (schema v1 `georeference` table) as required by issue #1 |
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

The native WebSocket/protobuf code must pass the updated Windows and Linux native workflows before those rows are promoted to **Verified**. The previous native baseline success proves only the earlier C++ executable foundation, not the new network implementation.

No entry may be promoted to **Implemented**, **Integrated**, or **Verified** without concrete source paths and verification evidence.
