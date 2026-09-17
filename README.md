# InfraForge

InfraForge is a native-first infrastructure authoring, geospatial editing, rendering, validation, and simulation platform.

## Status

InfraForge is being built from a clean repository. The project deliberately does not copy OpenGeoStudio's source tree or legacy coupling. Architecture, contracts, process boundaries, persistence rules, and UI composition are established before production feature work.

OpenGeoStudio-Qt is the workflow benchmark for InfraForge's UI refactor. InfraForge may match its productive interaction model—persistent editor, stable Navigator/Inspector regions, direct manipulation plus exact numeric editing, and contextual profile/cross-section tools—without copying its source tree, data model, renderer, transport, or process boundaries.

Current implementation state is tracked in `docs/IMPLEMENTATION_STATUS.md`. A document describing a future capability does not mean that capability is implemented.

## Product experience

InfraForge uses one persistent desktop editor with domain workspaces instead of disconnected feature pages. The target workspace model is documented in:

- `docs/06_UI_UX/APP_SHELL.md`
- `docs/06_UI_UX/UX_SPEC.md`
- `docs/06_UI_UX/WORKSPACE_MODEL.md`
- `docs/06_UI_UX/DESIGN_SYSTEM.md`
- `docs/UI_UX_ARCHITECTURE.md`
- `docs/UI_UX_ROADMAP.md`

The shared interaction grammar is:

`Select -> Create/Edit -> Inspector -> Context Editor (when needed) -> Validate -> Undo/Redo`

Global project actions, Problems, Operations, selection identity, project revision, and engine/renderer state remain consistent across workspaces.

## Architectural rules

1. The C++ backend owns canonical project/domain state.
2. The React frontend owns presentation and transient UI state only.
3. Frontend/backend communication uses a versioned WebSocket contract.
4. Electron is a thin desktop shell; business logic does not live in Electron.
5. Vulkan renders derived scene data; renderer resources are never canonical project data.
6. Generated meshes, GPU resources, and caches are disposable and rebuildable.
7. Every production feature must trace through UI -> contract -> application service -> domain -> persistence/render invalidation as applicable.
8. No fake success responses, mock production handlers, fabricated progress, silent fallback data, or placeholder production code.
9. Project georeferencing is canonical and shared by all domains.
10. Large-world behavior is designed around spatial partitioning and incremental invalidation from the beginning.
11. New domain UI must integrate with the shared workspace/command/selection/Navigator/Inspector/Problems/Operations model rather than create another application shell.

## Repository layout

```text
apps/          Desktop shell and frontend application
contracts/     Versioned transport schemas
engine/        Native C++ core engine (domain, application, persistence, network)
viewport/      Native Vulkan viewport process and platform surface integration
packages/      Shared TypeScript packages
shaders/       Renderer shader sources
tools/         Developer/build tooling
docs/          Product, technical, UX, data, protocol, and engineering specifications
```

Automated verification tests are organized within their owning subsystems (`engine/tests/`, `viewport/tests/`, `apps/*/tests/`, and `tools/engine-smoke/`).

For instructions on building prerequisites, native toolchain configuration, and running the desktop application, see `docs/07_ENGINEERING/BUILDING.md`.

## Source of truth

The specification hierarchy is:

1. `docs/00_PRODUCT/PRD.md`
2. accepted Architecture Decision Records under `docs/ADR/`
3. technical specifications under `docs/01_ARCHITECTURE/` through `docs/07_ENGINEERING/`
4. versioned protocol schemas under `contracts/`
5. production code

When documents and code disagree, the mismatch must be resolved explicitly; it must not be hidden by compatibility shims or silent fallback behavior.
