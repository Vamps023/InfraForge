# Workspace model

This document is the canonical UX blueprint for how InfraForge workspaces fit into one editor shell.

## Shared workspace contract

Every workspace provides a `WorkspaceDefinition` with:

- stable workspace ID and label;
- production availability/feature gate;
- tool groups and commands;
- default Navigator tab/filter;
- selectable canonical entity/sub-element types;
- Inspector section contributions;
- optional context editor contribution;
- optional viewport interaction controller;
- status/help hints;
- workspace-specific shortcuts where they do not conflict with global commands.

A workspace never owns canonical project/domain data. It configures presentation and command access around engine-owned state.

## Shared shell

All workspaces use the same persistent regions:

1. Global application/menu bar.
2. Workspace switcher.
3. Context tool shelf.
4. Navigator: Scene / Layers / Assets / Sources.
5. Main 2D/3D viewport.
6. Inspector.
7. Optional context editor.
8. Problems / Operations / Console / Performance-Simulation utility area.
9. Status bar.

## Shared authoring loop

`Select -> Create/Edit -> Inspect/Numeric Edit -> Context Edit -> Validate -> Undo/Redo`

Tool-specific variations must preserve the same cancellation, selection, snapping, feedback, diagnostics, and command semantics.

## Workspace matrix

| Workspace | Primary purpose | Main tools | Navigator emphasis | Context editor |
|---|---|---|---|---|
| Home / Project | lifecycle and entry | New, Open, Recent, Recovery | none/project summary | none |
| World | project spatial setup and source alignment | Geo settings, AOI, source inspect | Sources, Scene | optional map/source review |
| Terrain | terrain acquisition and inspection | Import, Download Area, Select/Inspect | Scene, Sources | optional coverage/table tools |
| Roads | horizontal/vertical road authoring | Select, Create, Plan Edit, Profile | Scene, Sources | road profile |
| Lanes & Junctions | lane semantics and topology | Lane Section, Cross-section, Marking, Junction | Scene | cross-section/topology |
| Infrastructure | semantic roadside/traffic objects | Place/Edit supported infrastructure | Scene, Assets | controller phases when applicable |
| Environment & Assets | visual content and placement | Browse/Import/Place/Transform | Assets, Scene | material/asset tools when justified |
| Rail | rail alignment/topology | Track Plan, Profile/Cant, Switch/Topology | Scene, Sources | profile/cant/topology |
| Scenario | authored scenario content | actors/routes/events when supported | Scene | timeline/event editor |
| Simulation | runtime configuration/execution | Configure, Run, Pause, Step, Reset | Scene | metrics/results |

## Home / Project

Home is deliberately small. It exposes project lifecycle, recent/recovery information, version/support information, and a short path into the next real task. It is not a dashboard full of domain cards.

## World

World owns the user's understanding of project coordinate context, not the canonical Geo implementation. It presents CRS/origin, traffic side, project area, floating-origin diagnostics, and imported sources. Changing interpretation/transformation semantics must be explicit and engine validated.

## Terrain

Terrain prioritizes acquisition and coverage clarity. The default flow is Import/Download -> review plan -> run operation -> inspect dataset/coverage -> render/sample -> diagnose. Download Area should feel like a focused tool inside the workspace, not a separate app.

## Roads

Roads prioritizes plan geometry in the viewport and vertical profile in the context editor. The active tool clearly distinguishes creating, selecting, moving controls, inserting controls, and other supported operations. Imported source geometry can be toggled/compared without becoming canonical truth.

## Lanes & Junctions

Lane editing should not overload the Road Plan tool. Lane/cross-section semantics and junction movement topology are explicit tasks with dedicated tool/context surfaces while sharing road selection and canonical IDs.

## Infrastructure

Infrastructure placement is constrained by road/lane semantics where applicable. The Inspector clearly separates semantic binding from visual appearance. Controller relationships are edited explicitly; decorative mesh placement must not masquerade as semantic infrastructure.

## Environment & Assets

The Asset Browser is a real project asset catalog, not a generic filesystem picker. Placement uses the same selection/transform grammar as other authoring workspaces. Source/relink/missing states remain visible.

## Rail

Rail shares domain-neutral alignment interaction ideas with Roads while keeping rail terminology, topology, gauge/cant, switches, signalling, and validation distinct.

## Scenario

Scenario authoring presents persistent scenario entities through the same Scene/Inspector model. A timeline or event graph is contextual, not a second project truth system.

## Simulation

Simulation is execution-oriented. Runtime actors/results are clearly distinguished from canonical authored data. Run controls remain visible and consistent while Problems/Operations surface failures and long-running work.

## Workspace switching rules

- Preserve project/session state.
- Preserve user layout preferences.
- Preserve compatible selection.
- Never silently commit an active preview because of a workspace switch.
- If a tool must be cancelled, show predictable cancellation behavior.
- Keep Problems and Operations global.
- Keep Save/Undo/Redo global.
- Hide unavailable workspaces in release UI unless a product decision explicitly requires a disabled preview entry.

## Layout defaults

Each workspace may define a recommended layout profile, but the user's customized layout wins. A workspace can request the context editor open/closed by default only when the user has not already customized that layout.

## Acceptance requirement

No new domain workspace is accepted if it introduces a separate top-level navigation system, duplicate selection store, duplicate command path, duplicate job/diagnostic UI, or domain-specific canonical state in React.
