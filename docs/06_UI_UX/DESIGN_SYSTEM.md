# Design system

## Visual direction

InfraForge uses a dark-first professional-editor visual system with restrained decoration, high information clarity, and maximum viewport space. It should feel precise and calm rather than flashy or dashboard-like.

The design borrows interaction lessons from OpenGeoStudio and established engineering/DCC tools, but InfraForge keeps its own visual identity and architecture.

## Design principles

1. **Simple first.** Show the controls required for the current task; reveal advanced options progressively.
2. **Stable geography.** Global controls, Navigator, Inspector, context editor, Problems/Operations, and status remain in predictable locations.
3. **Viewport dominance.** Chrome supports the work; it does not compete with the world view.
4. **Dense, not cramped.** Compact controls and small radii are preferred, but labels, units, focus states, and hit targets remain readable and accessible.
5. **State is explicit.** Active tool, hover, selection, disabled state, warnings, errors, locks, dirty/save state, and long-running operations must be easy to distinguish.
6. **No fake polish.** A beautiful placeholder is not a feature. Release UI exposes real production paths.

## Token groups

- surface: `background-0`, `background-1`, `background-2`, `panel`, `raised`, `input`;
- borders: `border`, `border-strong`, `focus`;
- text: `text-primary`, `text-secondary`, `text-muted`, `text-disabled`;
- semantic: `accent`, `selection`, `hover-selection`, `success`, `warning`, `error`, `info`;
- interaction: `hover`, `pressed`, `drag-target`, `drop-target`;
- domain overlays: reserved semantic colors for source geometry, canonical geometry, invalid geometry, locked state, and diagnostic emphasis where needed.

Feature code consumes semantic tokens; it must not introduce arbitrary one-off color constants for common UI states.

## Typography

- Default UI text is compact and highly legible.
- Section titles are understated; hierarchy comes primarily from spacing, weight, and separators rather than oversized headings.
- Engineering values use tabular numerals where practical.
- Units are visible but visually secondary to the value.
- Status/metadata text must remain readable at 100% and common Windows scaling values.

## Density and geometry

- Compact desktop control heights are the default.
- Typical corner radii remain small (roughly 2–6 px) unless a component has a specific reason to differ.
- Large rounded card layouts should be limited to Home/Project empty states and onboarding surfaces, not core authoring UI.
- Resizable panel handles should be visually quiet until hover/focus/drag.

## Core components

- Button / IconButton / SplitButton
- Input / SearchField / FilterField
- NumberInput with unit presentation and validation state
- Select / ComboBox
- Slider / RangeInput
- Checkbox / Toggle
- Tree / TreeRow / VirtualTree
- DataGrid
- PropertyGroup / PropertyRow
- Tabs / TabStrip
- Toolbar / ToolButton / ToolGroup
- WorkspaceSwitcher
- Panel / Dock container / ResizeHandle
- ContextEditorHost
- ContextMenu
- Popover
- Dialog / ConfirmDialog
- CommandPalette
- Progress / JobStatus
- DiagnosticRow
- StatusIndicator
- Tooltip
- Breadcrumb / SelectionPath where useful
- EmptyState
- SegmentedControl for compact mode choices

## Tool buttons

Every tool button must expose:

- icon;
- visible label when space/clarity requires it;
- tooltip with action and shortcut;
- active state;
- disabled reason when unavailable;
- accessible name;
- consistent cursor/interaction behavior.

Workspace tool shelves should be grouped by workflow rather than by implementation module. Large walls of small icons without discoverable labels are not acceptable.

## Inspector patterns

Inspector sections use stable order where applicable:

1. Identity
2. Geometry
3. Semantics
4. Appearance
5. Connections
6. Source / Provenance
7. Export
8. Diagnostics

Common properties are expanded by default. Advanced/rare groups may be collapsed. Validation errors should appear beside the responsible field/section as well as in Problems when they are project-significant.

## Selection and comparison colors

Hover, selection, source/reference geometry, canonical geometry, and errors must not share indistinguishable visual treatment.

For workflows such as OSM source-vs-canonical road review, the design system must support simultaneously visible comparison states without relying only on color; line style, width, handles, labels, or legend cues should reinforce meaning.

## Icons and labels

- Prefer familiar, domain-appropriate symbols.
- Icon-only controls require tooltips and accessible labels.
- Critical/destructive actions should include text in menus/dialogs.
- Avoid inventing different icons for the same global command in different workspaces.

## Feedback

- Hover: subtle background/outline change.
- Selected: clear accent treatment and/or geometry highlight.
- Active tool: persistent visual state until tool exit.
- Focus: visible keyboard focus outline.
- Disabled: reduced emphasis plus discoverable reason where useful.
- Warning/error: semantic icon + text, never color alone.
- Drag/drop: explicit target and invalid-target feedback.

## Motion

Transitions are short and functional (approximately 100–200 ms for standard state changes). Avoid large animated panel movement that slows expert workflows. Long-running work is represented by real operation state, not decorative animation.

## Ownership

Reusable primitives live in the shared design-system/UI package. Feature folders may compose primitives but must not fork equivalent controls, selection states, property patterns, tooltips, or job/diagnostic representations.
