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
| Native dependency manifest | Implemented, pending new CI verification | `vcpkg.json` pinned to immutable registry baseline |
| Protocol Buffer foundation schema | Implemented, pending new CI verification | `contracts/proto/infraforge/protocol/v1/foundation.proto` |
| TypeScript protocol generation | Implemented, pending new CI verification | Buf config + `@infraforge/protocol`; generated output is build output |
| Authenticated loopback WebSocket server | Implemented, pending build/runtime verification | `engine/src/network/WebSocketServer.cpp`; accepts authenticated hello and ping only |
| Electron desktop shell | Implemented | secure `main.ts`/`preload.ts`, no domain logic |
| Desktop engine supervision | Implemented, pending build/runtime verification | `apps/desktop/src/EngineSupervisor.ts`; random token, bounded bind retries, readiness validation |
| React editor shell | Implemented | `apps/frontend/src/App.tsx`, `styles.css`, UI-only Zustand store |
| Frontend authenticated engine session | Implemented, pending build/runtime verification | `apps/frontend/src/lib/engineSession.ts` |
| Design-system specification | Implemented as specification | `docs/06_UI_UX/DESIGN_SYSTEM.md` |
| Project persistence | Not implemented | No SQLite schema/runtime yet |
| Graceful project-aware engine shutdown | Not implemented | No project transaction lifecycle exists yet; current supervisor terminates child on app quit |
| Vulkan renderer | Not implemented | No native renderer yet |
| Canonical georeference runtime | Not implemented | Design only |
| World partition runtime | Not implemented | Design only |
| Terrain | Not implemented | Design only |
| Roads/lanes/junctions | Not implemented | Design only |
| Traffic infrastructure | Not implemented | Design only |
| Traffic simulation | Not implemented | Design only |
| Rail | Not implemented | Design only |

## Verification rule

The new transport/protocol code must pass the updated native and web/desktop CI workflows before those rows are promoted to **Verified**. Native baseline verification from the previous successful workflow is recorded separately and is not used to claim the new network code is verified.

No entry may be promoted to **Implemented**, **Integrated**, or **Verified** without concrete source paths and verification evidence.