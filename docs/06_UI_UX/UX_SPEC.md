# InfraForge UX specification

## UX objective

InfraForge is a viewport-first professional desktop editor. Navigation is mode/tool based, not a set of disconnected full-page web routes.

## UI direction

InfraForge adopts a dense, engineering-editor interaction model informed by the
OpenGeoStudio editor experience: a persistent viewport surrounded by a
workspace rail, contextual tools, a navigable scene hierarchy, an inspector,
and durable diagnostic/operation surfaces. This is a UX reference only. No
OpenGeoStudio source, architecture, state model, renderer, or protocol is an
InfraForge implementation dependency.

The intended result is a coherent professional shell, not a visual clone:

- the native viewport remains the dominant working area;
- every workspace changes tools and projections, not the underlying shell;
- selection is one shared presentation state synchronized among viewport,
  outliner, inspector, and Problems;
- menus, toolbar buttons, context menus, shortcuts, and the command palette
  invoke the same registered command;
- panels are compact, information-dense, resizable, and recoverable at common
  Windows DPI scales;
- unavailable capabilities are visibly unavailable, never simulated.

## Persistent layout

```text
┌────────────────────────────────────────────────────────────────────┐
│ Menu bar                                                           │
├────────────────────────────────────────────────────────────────────┤
│ Context toolbar                                                    │
├──────┬───────────────┬───────────────────────────┬─────────────────┤
│ Mode │ Outliner      │ Native 3D viewport        │ Inspector       │
│ rail │               │                           │                 │
├──────┴───────────────┴───────────────────────────┴─────────────────┤
│ Problems | Operations | Console | Simulation                       │
├────────────────────────────────────────────────────────────────────┤
│ Status: coordinates | CRS | engine | renderer | FPS | project rev │
└────────────────────────────────────────────────────────────────────┘
```

The left dock may expose tabs such as **Scene**, **Layers**, and **Assets**
when their backing domain projections exist. The right dock uses progressive
disclosure: essential selection properties first, engineering/detail sections
second. The bottom dock is persistent enough that errors and long-running work
do not disappear behind transient notifications.

## Mode rail

Initial mode model:

- World
- Terrain
- Road
- Rail
- Infrastructure
- Assets
- Scenario
- Simulation

Unavailable/unimplemented modes must be explicitly disabled or absent; they must not open fake workspaces.

## Outliner

The outliner supports hierarchy, search, domain filter, visibility where meaningful, lock where meaningful, multi-selection, rename where allowed, and context actions. Large trees are virtualized.

## Inspector

Inspector content is selection driven. Property sections are supplied by the owning feature/domain UI adapter. Edits send commands; the inspector does not mutate cached canonical objects locally and pretend the backend accepted them.

## Operations panel

Long jobs appear with real lifecycle states. Percentage is shown only when the backend provides defensible progress. Failure remains visible until acknowledged/resolved; it is not only a toast.

## Problems panel

Diagnostics include severity, source/domain, message, entity link when applicable, and optional suggested action. Selecting an entity diagnostic can frame/select the affected object when the viewport/domain supports it.

## Command palette

All significant commands register with one command service containing ID, label, availability predicate, shortcut, and invoke action. Menus/toolbars reference the same command registrations rather than duplicating behavior.

## Interaction qualities

- Dense professional controls rather than dashboard cards.
- Compact spacing and restrained corner radius.
- Strong focus/hover/selection states.
- Keyboard accessible controls.
- Predictable numeric editing.
- No destructive action hidden behind icon-only ambiguity.
- Clear units on engineering values.
- Errors stay associated with the action/property that caused them when possible.
- Viewport navigation, selection, framing, and tool cancellation have visible
  affordances and documented shortcuts; focus must never be trapped in a dock.
