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
| Native C++ engine executable foundation | Implemented, not yet network-integrated | `engine/src/main.cpp`, `engine/CMakeLists.txt`; supports real `--version`, `--help`, `--self-check` only |
| Native build CI | Implemented | `.github/workflows/native-build.yml` builds/self-checks Windows and Linux on GitHub Actions |
| Electron desktop shell foundation | Implemented, not yet engine-integrated | `apps/desktop/src/main.ts`, secure `preload.ts` |
| React editor shell foundation | Implemented, not yet engine-integrated | `apps/frontend/src/App.tsx`, `styles.css`, UI-only Zustand store |
| Design-system specification | Implemented as specification | `docs/06_UI_UX/DESIGN_SYSTEM.md` |
| WebSocket protocol specification | Implemented as specification | `docs/03_PROTOCOL/WEBSOCKET_PROTOCOL.md` |
| Protocol Buffer schemas/generation | Not implemented | No committed `.proto` schema/generation pipeline yet |
| Authenticated WebSocket server | Not implemented | Engine intentionally exposes no serve mode yet |
| Desktop engine supervision | Not implemented | Electron does not launch `infraforge-engine` yet |
| Frontend engine client | Not implemented | Frontend does not claim a connected engine session |
| Project persistence | Not implemented | No SQLite schema/runtime yet |
| Vulkan renderer | Not implemented | No native renderer yet |
| Canonical georeference runtime | Not implemented | Design only |
| World partition runtime | Not implemented | Design only |
| Terrain | Not implemented | Design only |
| Roads/lanes/junctions | Not implemented | Design only |
| Traffic infrastructure | Not implemented | Design only |
| Traffic simulation | Not implemented | Design only |
| Rail | Not implemented | Design only |

## Verification note

The native GitHub Actions workflow has been committed but its run result must be checked before the native executable is marked **Verified**. The frontend/desktop package files are committed without a lockfile and have not been claimed build-verified yet.

No entry may be promoted to **Implemented**, **Integrated**, or **Verified** without concrete source paths and verification evidence.