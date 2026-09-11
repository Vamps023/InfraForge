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
| Protocol Buffer foundation schema | Web-side verified; native-side pending | `contracts/proto/infraforge/protocol/v1/foundation.proto`; Buf lint and TypeScript generation passed on commit `c610031f9c027b2ccc3797da4c3a5a190dc5dacf` |
| TypeScript protocol generation | Verified | Buf generation + strict TypeScript + production frontend build passed on commit `c610031f9c027b2ccc3797da4c3a5a190dc5dacf` |
| Authenticated loopback WebSocket server | Implemented, pending native build/runtime verification | `engine/src/network/WebSocketServer.cpp`; accepts authenticated hello and ping only |
| Electron desktop shell | Build verified | secure `main.ts`/`preload.ts`; desktop TypeScript/build passed on commit `c610031f9c027b2ccc3797da4c3a5a190dc5dacf` |
| Desktop engine supervision | Build verified; runtime integration pending native build | `apps/desktop/src/EngineSupervisor.ts`; random token, bounded bind retries, readiness validation |
| React editor shell | Build verified | `apps/frontend/src/App.tsx`, `styles.css`, UI-only Zustand store; strict TypeScript and production build passed |
| Frontend authenticated engine session | Build verified; end-to-end runtime pending native build | `apps/frontend/src/lib/engineSession.ts` |
| Web/desktop CI | Verified | protocol lint, strict TypeScript, frontend build, and desktop build succeeded on GitHub Actions run for commit `c610031f9c027b2ccc3797da4c3a5a190dc5dacf` |
| Design-system specification | Implemented as specification | `docs/06_UI_UX/DESIGN_SYSTEM.md` |
| Project persistence | Not implemented | GitHub issue #1; no SQLite schema/runtime yet |
| Graceful project-aware engine shutdown | Not implemented | No project transaction lifecycle exists yet; current supervisor terminates child on app quit |
| Vulkan renderer | Not implemented | GitHub issue #2; no native renderer yet |
| Canonical georeference runtime | Not implemented | GitHub issue #3; design only |
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