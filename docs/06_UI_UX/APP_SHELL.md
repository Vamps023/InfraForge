# App shell specification

The app shell owns persistent editor composition, not domain behavior.

## Regions

1. Menu bar
2. Context toolbar
3. Mode rail
4. Left dock (outliner/content browser)
5. Central native viewport host
6. Right dock (inspector)
7. Bottom dock (problems/operations/console/simulation)
8. Status bar

## Layout persistence

Panel size/visibility/docking preferences are user preferences. They are not canonical project state. Project-specific workspace state may be stored separately only when explicitly designed.

## Engine state

The shell visibly distinguishes `starting`, `ready`, `disconnected`, `failed`, and `shutting_down`. Domain controls requiring the engine are disabled when the engine session is unavailable.

## Empty state

Before a project is open, the viewport/shell shows project actions and recent projects without inventing world data.

## Native viewport

The viewport host must correctly handle resize, focus, pointer capture, DPI scaling, maximize/restore, multi-monitor movement, tab/dock visibility, destruction, and renderer failure. This lifecycle is a Phase 4 acceptance gate before geometry authoring expands.