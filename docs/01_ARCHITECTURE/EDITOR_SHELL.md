# Editor shell architecture

Issue #5: reusable production-grade editor framework — command registry, docking layout, outliner, inspector, operations, and problems/diagnostics.

This document describes the editor shell architecture implemented in `apps/frontend/src/editor/`. The shell is infrastructure for future terrain, roads, lanes, junctions, infrastructure, simulation, and other domains. No fake domain features were added to populate the UI.

## Architectural rules preserved

- The C++ engine owns canonical project/domain state (ADR-0009). Frontend stores are projections/presentation only.
- Selection is expressed as stable canonical IDs owned by the engine, never frontend object identity.
- No fake production success, mock responses, fabricated progress, dummy world data, or silent fallback (ADR-0010).
- The native viewport continues to behave correctly with panel resize/hide/show and dialogs (ADR-0011); the existing viewport visibility policy is unchanged.
- Electron remains a thin shell (ADR-0003); the command registry and menu live in the renderer, not in Electron main.

## 1. Command registry (`editor/commands/`)

**Ownership:** `commandRegistry.ts` is the single source of command definitions for application menus, toolbars, keyboard shortcuts, and the future command palette.

- Each command has a stable ID, label, description, category, group, optional shortcut metadata, execute handler, optional enabled/visible predicates, and engine/project/viewport availability requirements.
- Menus (`AppMenu.tsx`), toolbars (`Toolbar.tsx`), and the keyboard shortcut handler (`useCommands.ts`) all reference registered commands by ID and resolve availability through `resolveCommandAvailability`. No command execution logic is duplicated across surfaces.
- Existing real project operations (New, Open, Save, Close, Georeference) migrated to `builtinCommands.ts` and issue real engine commands through the existing `projectApi`/`geoApi` paths.
- Duplicate command IDs are rejected at registration time.
- The keyboard shortcut handler skips text-input contexts and matches on key + modifiers (Ctrl/Cmd, Shift, Alt).

## 2. Engine/project availability predicates (`editor/availability.ts`)

**Ownership:** centralized gating policy. Commands and shell components query `deriveAvailability` and `evaluateAvailability` instead of independently re-deriving engine/project state.

States modeled:
- Engine: `starting`, `ready`, `disconnected`, `failed`, `unavailable`
- Project: `no-project`, `project-open`, `busy`
- Viewport: active/inactive (reuses the existing `viewportSurfaceActive` predicate)

Commands declare `requiresEngine`, `requiresProject`, `requiresViewport`. The registry evaluates these centrally; components do not scatter gating checks.

## 3. Dockable/resizable layout (`editor/layout/`)

**Ownership:** panel visibility and sizes are USER editor preferences, persisted separately from canonical project state (ADR-0009) under `localStorage` key `infraforge.editor.layout.v1`. They never enter project files.

- Left, right, and bottom panel regions plus a central viewport/workspace.
- Panel visibility toggles, pointer-drag resizing, and keyboard-nudgeable resize handles (`ResizeHandle.tsx`) with ARIA separator semantics.
- Practical minimum sizes enforced per region (`PANEL_MIN_SIZE`).
- Preferences persist on every change and restore on load; corrupt/partial storage falls back to defaults.
- The native viewport host lives in the center; its `ResizeObserver` re-reports bounds on every layout change so panel resize/hide/show re-places the native surface without regressing the viewport visibility policy.

## 4. Outliner framework (`editor/outliner/`)

**Ownership:** the outliner consumes backend/domain projections through a registered projection API; it never owns canonical objects.

- `outlinerProjection.ts` defines the `OutlinerProjection` contract (stable canonical IDs, parent/child relationships, labels, object type, depth, hasChildren) and a registry.
- Future domains (terrain, roads, junctions, infrastructure, simulation) register projections; the outliner composes all registered projections into one tree. No fake road/terrain entities are injected.
- `outlinerTree.ts` flattens the composed nodes into the visible row list by walking roots and expanding only expanded parents (expansion keyed by canonical ID).
- `Outliner.tsx` virtualizes the visible rows (windowed rendering with overscan) so large projects never render thousands of DOM rows simultaneously.
- Selection resolves canonical IDs through the shared `selectionStore`; the outliner never substitutes its own object identity.

## 5. Inspector section registry (`editor/inspector/`)

**Ownership:** `inspectorRegistry.ts` provides an extensible registration/composition model. The shell does not hard-code future domains into one giant inspector component.

- Each section declares `applies(selectionContext)` and `render(selectionContext)`.
- The inspector resolves applicable sections by asking each registered section whether it applies to the current canonical selection, then renders them in `order` order.
- Stable selection identity, empty selection state, and unsupported selection handling are all modeled honestly.
- The only registered section today is `projectOverviewSection.tsx`, which displays the real canonical project summary projected from the engine. No fake road/terrain properties are injected.

## 6. Operations model (`editor/operations/`)

**Ownership:** `operationsStore.ts` is a real framework for long-running backend work, driven by server-projected operation/job events. No fake running jobs are created; the store starts empty and only gains entries when the backend projects operation events.

- Each operation carries a stable ID, name, type, state, progress (when provided), processed/total work (when available), diagnostics/error state, source, and target canonical ID.
- Cancellation is exposed in the model only when the backend actually supports it. The current protocol does not define a cancellation command, so `cancellable` defaults to `false` and the cancel UI is disabled. The architecture is clean without pretending cancellation works.

## 7. Problems/diagnostics model (`editor/problems/`)

**Ownership:** `problemsStore.ts` is driven by backend events/projections. No static sample warnings are created; the store starts empty.

- Each diagnostic carries severity, message, source/domain, canonical object ID (for navigation/selection hooks), and a stable identity.
- `replaceSource` supports full replacement of a source's diagnostics according to backend semantics.
- `useProblemDiagnostics.ts` bridges the existing real frontend-owned surfaces (engine session errors, viewport failures) into the store as diagnostics. Backend-projected diagnostics will arrive through event subscriptions added by future domains.

## 8. Selection identity (`editor/selection/`)

**Ownership:** `selectionStore.ts` is the single selection surface for the shell, outliner, inspector, and any future command that acts on the current selection.

- Selection is expressed as stable canonical IDs owned by the engine. The frontend never substitutes object identity for canonical identity.
- Replace/add/toggle modes; primary selection tracking; clear.
- The 2D/3D architecture rule remains intact: all views eventually operate on the same canonical world and IDs.

## 9. Accessibility and keyboard behavior

- Logical tab order through the shell; resize handles are keyboard-operable (arrow nudge, Shift+arrow large nudge, Home reset) with ARIA separator semantics.
- Visible focus styles on menu triggers, menu entries, toolbar buttons, and resize handles.
- ARIA semantics: menu triggers use `aria-haspopup`/`aria-expanded`; menu entries use `role="menuitem"`; the outliner uses `role="tree"`/`role="treeitem"`/`aria-selected`; resize handles use `role="separator"`/`aria-orientation`.
- The keyboard shortcut handler skips text-input contexts so editor fields keep normal key behavior and shortcut conflicts do not block typing.

## Limitations still pending

- No domain projections are registered yet (terrain/roads/junctions/infrastructure/simulation are not implemented — issues #6–#13). The outliner and inspector render honest empty states.
- No backend operation events are projected yet; the operations store starts empty and will be populated when a domain issues long-running work.
- No backend diagnostic events are projected yet; the problems store surfaces only the real shell-owned surfaces (engine session errors, viewport failures).
- Cancellation is modeled but not wired because the protocol does not yet support it.
- The command palette is not yet built; the registry is ready for it.
- Runtime validation against the live native viewport is pending the desktop runtime; the existing viewport lifecycle/policy tests remain green and the layout changes do not alter the visibility policy path.
