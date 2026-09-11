# InfraForge UX specification

## UX objective

InfraForge is a viewport-first professional desktop editor. Navigation is mode/tool based, not a set of disconnected full-page web routes.

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

- Compact spacing.
- Restrained corner radius.
- Strong focus/hover/selection states.
- Keyboard accessible controls.
- Predictable numeric editing.
- No destructive action hidden behind icon-only ambiguity.
- Clear units on engineering values.
- Errors stay associated with the action/property that caused them when possible.