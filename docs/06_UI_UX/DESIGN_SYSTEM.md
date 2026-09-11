# Design system

## Visual direction

InfraForge uses a dark-first professional-editor visual system. Decoration is subordinate to information density and viewport space.

## Token groups

- surface: `background-0`, `background-1`, `background-2`, `panel`, `raised`;
- borders: `border`, `border-strong`, `focus`;
- text: `text-primary`, `text-secondary`, `text-disabled`;
- semantic: `accent`, `selection`, `success`, `warning`, `error`, `info`;
- interaction: `hover`, `pressed`, `drag-target`.

Feature code consumes semantic tokens; it must not introduce arbitrary one-off color constants for common UI states.

## Core components

- Button / IconButton
- Input / SearchField
- NumberInput with unit presentation
- Select / ComboBox
- Slider
- Checkbox / Toggle
- Tree / TreeRow
- DataGrid
- PropertyGroup / PropertyRow
- Tabs
- Toolbar / ToolButton
- Panel / Dock container
- ContextMenu
- Popover
- Dialog
- CommandPalette
- Progress / JobStatus
- DiagnosticRow
- StatusIndicator
- Tooltip

## Density

Controls default to compact desktop sizing. Typical corner radii remain small (roughly 2–6 px) unless a specific component requires otherwise.

## Ownership

Reusable primitives live in the shared design-system package. Feature folders may compose primitives but must not fork equivalent core controls.