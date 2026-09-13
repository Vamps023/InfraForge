# Editor shell architecture

Issue #5: reusable production-grade editor framework — command registry, docking layout, outliner, inspector, operations, and problems/diagnostics.

This document describes the editor shell architecture implemented in `apps/frontend/src/editor/`. The shell is infrastructure for future terrain, roads, lanes, junctions, infrastructure, simulation, and other domains. No fake domain features were added to populate the UI.

## Architectural rules preserved

- The C++ engine owns canonical project/domain state (ADR-0009). Frontend stores are projections/presentation only.
- Selection is expressed as stable canonical IDs owned by the engine, never frontend object identity.
- No fake production success, mock responses, fabricated progress, dummy world data, or silent fallback (ADR-0010).
- The native viewport continues to behave correctly with panel resize/hide/show and dialogs (ADR-0011); the existing viewport visibility policy is unchanged.
- Electron remains a thin shell (ADR-0003); the command registry and menu live in the renderer, not in Electron main.

## Observable registry pattern

All three shell registries (command, outliner projection, inspector section) use the same observable pattern:

- A `Map` holds the registered items.
- `subscribe(listener)` adds a listener to a `Set`; returns an unsubscribe function.
- `getSnapshot()` returns a cached array of all items, invalidated on mutation so the reference is stable between mutations (no infinite render loops in `useSyncExternalStore`).
- `register()`/`unregister()` notify all listeners after mutating the map.
- React components consume the snapshot via `useSyncExternalStore(subscribe, getSnapshot)` so they re-render automatically when membership changes after mount — no stale `useMemo([])` caches or per-component force-render hacks.

## 1. Command registry (`editor/commands/`)

**Ownership:** `commandRegistry.ts` is the single source of command definitions for application menus, toolbars, keyboard shortcuts, and the command palette.

- Each command has a stable ID, label, description, category, group, optional shortcut metadata, execute handler, optional enabled/visible predicates, and engine/project/viewport availability requirements.
- **Surfaces:** each command declares `surfaces?: Array<'menu' | 'toolbar' | 'shortcut' | 'palette'>` (defaults to `['menu']`). `surfaces` is the explicit placement concept — `category`/`group` remain for logical organization and are never misused as UI placement. The toolbar renders only commands whose `surfaces` include `'toolbar'`; the menu renders only commands whose `surfaces` include `'menu'`; the palette renders only commands whose `surfaces` include `'palette'`; the shortcut handler dispatches only commands whose `surfaces` include `'shortcut'`.
- **Shared helpers:** `isCommandVisible(command, context)` and `commandSurfaces(command)` are used by all surfaces (menu, toolbar, palette, shortcuts) so visibility and surface filtering behavior cannot drift.
- **Observable:** the registry exposes `subscribe`/`getSnapshot`. `AppMenu`, `Toolbar`, `CommandPalette`, and `useCommandShortcuts` consume it via `useSyncExternalStore` so they update automatically when commands are registered or unregistered after mount.
- **Central execution:** menu, toolbar, palette, and keyboard shortcuts all execute through `executeCommand(commandId, context)`, which gates on availability before invoking the handler. No surface duplicates the execution or gating logic.
- **Shortcut conflicts:** the registry rejects two commands claiming the same key+modifier combination with a deterministic error, rather than silently executing whichever was registered first.
- **Shortcut surface filtering:** the keyboard shortcut handler only dispatches commands whose effective `surfaces` include `'shortcut'`. A command with shortcut metadata but no `'shortcut'` surface does not execute from the keyboard. Invisible commands are not dispatched via shortcut.
- **Platform-aware display:** `shortcutDisplayLabel()` computes `Ctrl+` on Windows/Linux and `⌘` on macOS so shortcut labels are not hardcoded to one platform.
- Existing real project operations (New, Open, Save, Close, Georeference) migrated to `builtinCommands.ts` and issue real engine commands through the existing `projectApi`/`geoApi` paths.
- Duplicate command IDs are rejected at registration time.
- The keyboard shortcut handler skips text-input contexts (input, textarea, select, contentEditable) and matches on key + modifiers (Ctrl/Cmd, Shift, Alt).
- **`requiresEngine` defaults to false** (command does not require the engine). Set to true for commands that issue engine commands.

## 2. Engine/project availability predicates (`editor/availability.ts`)

**Ownership:** centralized gating policy. Commands and shell components query `deriveAvailability` and `evaluateAvailability` instead of independently re-deriving engine/project state.

States modeled:
- Engine: `starting`, `ready`, `disconnected`, `failed`, `unavailable`
- Project: `no-project`, `project-open`, `busy`
- Viewport: active/inactive (reuses the existing `viewportSurfaceActive` predicate)

**Pure derivation:** `deriveAvailability` takes all inputs as explicit parameters (engine status, engine session, project summary, project operation, viewport state) and never reads global Zustand stores via `getState()`. The `useCommandContext` hook subscribes to the stores reactively (via Zustand selectors) and passes the values in, so the context updates immediately on any state change.

**Busy gating:** commands declare `requiresNotBusy?: boolean` to prevent conflicting lifecycle operations. Project lifecycle commands (New, Open, Save, Close, Georeference) all declare `requiresNotBusy` or `requiresProject` (which also gates on busy), so concurrent lifecycle operations that the project API is not designed to serialize are blocked centrally.

Commands declare `requiresEngine`, `requiresProject`, `requiresViewport`, `requiresNotBusy`. The registry evaluates these centrally; components do not scatter gating checks.

## 3. Dockable/resizable layout (`editor/layout/`)

**Ownership:** panel visibility and sizes are USER editor preferences, persisted separately from canonical project state (ADR-0009) under `localStorage` key `infraforge.editor.layout.v1`. They never enter project files.

- Left, right, and bottom panel regions plus a central viewport/workspace.
- Panel visibility toggles, pointer-drag resizing, and keyboard-nudgeable resize handles (`ResizeHandle.tsx`) with ARIA separator semantics.
- **Keyboard semantics per edge:** ArrowRight grows the left panel, ArrowLeft grows the right panel, ArrowUp grows the bottom panel; the reverse arrows shrink. `Home` resets to the region-specific default (`PANEL_DEFAULT_SIZE[region]`), not a hardcoded constant. Shift+arrow nudges by 32px.
- **Validation:** `clampPanelSize` rejects `NaN`, `Infinity`, `-Infinity` (falls back to the region default) and enforces both minimum (`PANEL_MIN_SIZE`) and practical maximum (`PANEL_MAX_SIZE`) sizes so corrupted localStorage cannot make the editor unusable.
- The native viewport host lives in the center; its `ResizeObserver` re-reports bounds on every layout change so panel resize/hide/show re-places the native surface without regressing the viewport visibility policy.

## 4. Outliner framework (`editor/outliner/`)

**Ownership:** the outliner consumes backend/domain projections through a registered projection API; it never owns canonical objects.

- `outlinerProjection.ts` defines the `OutlinerProjection` contract (stable canonical IDs, parent/child relationships, labels, object type, depth, hasChildren) and an **observable** registry.
- **Reactivity:** the Outliner subscribes to the registry via `useSyncExternalStore` so registering/unregistering a projection after mount updates the tree. Each projection's `subscribe` is wired in an effect keyed on the current projection list, so projection-emitted updates recompute node data and subscriptions are cleaned up correctly (no stale subscriptions, no leaks, no duplicate subscriptions). A `projectionVersion` counter is included in the `useMemo` dependency array so projection data changes invalidate the composed node list and re-read `getNodes()`.
- `outlinerTree.ts` flattens the composed nodes into the visible row list by walking roots and expanding only expanded parents (expansion keyed by canonical ID).
- `Outliner.tsx` virtualizes the visible rows (windowed rendering with overscan) so large projects never render thousands of DOM rows simultaneously. The absolute row index is computed as `startIndex + localIndex` — no O(n) `indexOf` lookup per rendered row.
- **Search:** search runs across all projected nodes regardless of expansion state, including matches inside collapsed parents. Ancestor paths are included so hierarchy remains understandable. Search does not mutate permanent expansion state.
- Selection resolves canonical IDs through the shared `selectionStore`; the outliner never substitutes its own object identity.
- **Project root projection:** `projectRootProjection.ts` surfaces the real canonical project root as a single outliner node sourced from the backend-projected `ProjectSummary`. The node ID is the real `projectUuid`; the label is the real `displayName`. No fake children are generated. Reacts to project open/close/summary changes via the project store.
- **Empty-state semantics:** the Outliner distinguishes no project open, project open with no projected entities, and active search with no matches — using real project state, not node count as a proxy.
- **Keyboard navigation:** tree rows are focusable (`tabIndex`). ArrowUp/ArrowDown move focus between rows. ArrowRight expands a collapsed parent; ArrowLeft collapses an expanded parent. Enter/Space selects the focused row. `aria-expanded` reflects expansion state on parent rows.

## 5. Inspector section registry (`editor/inspector/`)

**Ownership:** `inspectorRegistry.ts` provides an extensible, **observable** registration/composition model. The shell does not hard-code future domains into one giant inspector component.

- Each section declares `applies(selectionContext)` and `render(selectionContext)`.
- The inspector subscribes to the section registry via `useSyncExternalStore` and to the selection store via Zustand, so it updates when sections are registered/unregistered after mount and when selection changes.
- The only registered section today is `projectOverviewSection.tsx`, which displays the real canonical project summary projected from the engine. No fake road/terrain properties are injected.
- **Project-root selection:** the Project Overview section applies when no entity is selected OR when the selected primary ID equals the canonical project UUID (the Outliner's project-root node). Selecting the project root in the Outliner shows the Project Inspector; selecting an unrelated entity does not.

## 6. Operations model (`editor/operations/`)

**Ownership:** `operationsStore.ts` is a real framework for long-running backend work, driven by server-projected operation/job events. No fake running jobs are created; the store starts empty and only gains entries when the backend projects operation events.

- Each operation carries a stable ID, name, type, state, progress (when provided), processed/total work (when available), diagnostics/error state, source, and target canonical ID.
- Cancellation is exposed in the model only when the backend actually supports it. The current protocol does not define a cancellation command, so `cancellable` defaults to `false` and the cancel UI is disabled. The architecture is clean without pretending cancellation works.
- The UI distinguishes: no operations ever received, active operations, and completed/failed recent operations.
- **Backend event ingestion is not yet implemented.** The protocol roadmap (`docs/03_PROTOCOL/EVENTS.md`) defines future `job.*` event families that will be introduced alongside production domain implementations. The operations store remains empty until the engine emits real job events and a shell-level event projector is wired. No fabricated events are inserted to make the UI populated.

## 7. Problems/diagnostics model (`editor/problems/`)

**Ownership:** `problemsStore.ts` is driven by backend events/projections. No static sample warnings are created; the store starts empty.

- Each diagnostic carries severity, message, source/domain, canonical object ID (for navigation/selection hooks), and a stable identity.
- **Stable diagnostic identity for shell-owned surfaces:** the viewport and engine-session diagnostics use a single stable ID per source (`viewport:status`, `engine-session:status`) so a new failure replaces (not accumulates with) the previous one, and recovery removes the diagnostic entirely. No stale Problems entries remain after the viewport recovers.
- `replaceSource` supports full replacement of a source's diagnostics according to backend semantics.
- `useProblemDiagnostics.ts` bridges the existing real frontend-owned surfaces (engine session errors, viewport failures) into the store as diagnostics.
- **Backend diagnostic event ingestion is not yet implemented.** The protocol roadmap (`docs/03_PROTOCOL/EVENTS.md`) defines future `diagnostic.added`/`diagnostic.removed`/`diagnostic.cleared` event families that will be introduced alongside production domain implementations. The problems store surfaces only the real shell-owned surfaces until the engine emits real diagnostic events and a shell-level event projector is wired. The future contract must support add/remove/clear lifecycle so resolved backend diagnostics do not remain forever.

## 8. Selection identity (`editor/selection/`)

**Ownership:** `selectionStore.ts` is the single selection surface for the shell, outliner, inspector, and any future command that acts on the current selection.

- Selection is expressed as stable canonical IDs owned by the engine. The frontend never substitutes object identity for canonical identity.
- Replace/add/toggle modes; primary selection tracking; clear.
- **Lifecycle boundaries:** selection is cleared when a project opens (canonical IDs from the previous project must not survive) and when a project closes (stale IDs must not resolve against a non-existent world). Selection is not cleared on revision or dirty-state changes (the project remains valid).
- The 2D/3D architecture rule remains intact: all views eventually operate on the same canonical world and IDs.

## 9. Accessibility and keyboard behavior

- Logical tab order through the shell; resize handles are keyboard-operable (arrow nudge per edge direction, Shift+arrow large nudge, Home resets to the region default) with ARIA separator semantics (`role="separator"`, `aria-orientation`, `aria-valuemin`, `aria-valuemax`, `aria-valuenow`). `aria-valuemin`/`aria-valuemax` use the real `PANEL_MIN_SIZE`/`PANEL_MAX_SIZE` for the region. Pointer-drag cleanup restores `document.body.style.userSelect` on pointer-up and on unmount-during-drag.
- Visible focus styles on menu triggers, menu entries, toolbar buttons, palette results, and resize handles.
- **Application menu:** `role="menu"`/`role="menuitem"`. ArrowDown/ArrowUp navigate within an open dropdown; Escape closes the dropdown; Enter activates the focused entry. Focus is tracked via a `focusIndex` state so navigation is deterministic.
- **Bottom panel tabs:** `role="tablist"`/`role="tab"` with `aria-selected`. ArrowLeft/ArrowRight move between tabs. Tab panels are linked via the active tab state.
- **Problems rows:** focusable (`tabIndex=0`) with `role="button"`. Enter/Space activates the row and selects the diagnostic's target canonical ID (when present). Mouse click remains supported.
- **Outliner tree:** `role="tree"`/`role="treeitem"` with `aria-selected`/`aria-expanded`. Rows are focusable (`tabIndex`). ArrowUp/ArrowDown move focus between rows; ArrowRight expands a collapsed parent; ArrowLeft collapses an expanded parent; Enter selects the focused row.
- **Command palette:** searchable, keyboard-navigable dialog. ArrowUp/ArrowDown move the active result; Enter executes the enabled result through the central executor; Escape closes; focus enters the search field on open. Disabled commands remain visibly disabled.
- ARIA semantics: menu triggers use `aria-haspopup`/`aria-expanded`; menu entries use `role="menuitem"`; the outliner uses `role="tree"`/`role="treeitem"`/`aria-selected`/`aria-expanded`; resize handles use `role="separator"`/`aria-orientation`/`aria-valuemin`/`aria-valuemax`/`aria-valuenow`; bottom panel tabs use `role="tablist"`/`role="tab"`/`aria-selected`.
- The keyboard shortcut handler skips text-input contexts so editor fields keep normal key behavior and shortcut conflicts do not block typing.

## Limitations still pending

- No domain projections are registered yet (terrain/roads/junctions/infrastructure/simulation are not implemented — issues #6–#13). The outliner renders the real project root plus honest empty states.
- No backend operation events are projected yet; the operations store starts empty and will be populated when a domain issues long-running work. The protocol roadmap (`docs/03_PROTOCOL/EVENTS.md`) defines future `job.*` event families that will be introduced alongside production domain implementations. No generic `OperationEvent` protocol message is committed until the engine implements emission.
- No backend diagnostic events are projected yet; the problems store surfaces only the real shell-owned surfaces (engine session errors, viewport failures). The protocol roadmap defines future `diagnostic.added`/`diagnostic.removed`/`diagnostic.cleared` event families. The future contract must support add/remove/clear lifecycle so resolved backend diagnostics do not remain forever. No generic `DiagnosticEvent` protocol message is committed until the engine implements emission.
- Cancellation is modeled but not wired because the protocol does not yet support it.
- Runtime validation against the live native viewport is pending the desktop runtime; the existing viewport lifecycle/policy tests remain green and the layout changes do not alter the visibility policy path.
