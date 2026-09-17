# App shell specification

The app shell owns persistent editor composition, not domain behavior.

## Reference-informed composition

The shell follows a professional geospatial/CAD-editor composition: a compact
application header, vertical workspace rail, contextual toolbar, docked scene
navigation, maximum-area native viewport, selection inspector, bottom
diagnostics/operations dock, and a concise status bar. OpenGeoStudio is a UX
reference for this composition only; InfraForge retains its own React,
WebSocket/Protobuf, native-engine, and Vulkan process boundaries.

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

The shell must provide a safe reset-layout action and preserve a usable
viewport, keyboard focus path, and reachable panel headers after DPI or
multi-monitor changes. A workspace may contribute dock content only through
shell-defined presentation interfaces; it may not introduce another app shell
or a cross-domain canonical store.

## Engine state

The shell visibly distinguishes `starting`, `ready`, `disconnected`, `failed`, and `shutting_down`. Domain controls requiring the engine are disabled when the engine session is unavailable.

## Empty state

Before a project is open, the viewport/shell shows project actions and recent projects without inventing world data.

## Native viewport

The viewport host must correctly handle resize, focus, pointer capture, DPI scaling, maximize/restore, multi-monitor movement, tab/dock visibility, destruction, and renderer failure. This lifecycle is a Phase 4 acceptance gate before geometry authoring expands.

Viewport overlays and contextual authoring controls are presentation aids.
They must route semantic operations through the existing command path and must
not create renderer-owned or frontend-owned road, terrain, CRS, or project
truth.
