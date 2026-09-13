# ADR-0011: Dedicated native viewport process with OS child surface embedding

- Status: Accepted
- Date: 2026-09-12

## Context

InfraForge requires a high-performance native 3D viewport (Vulkan 1.3) embedded inside a desktop editing shell (Electron/Chromium). The rendering subsystem must handle OS window events, display changes, multi-monitor mixed-DPI transitions, and GPU resource lifecycles.

Three architectural options were considered:
1. **In-process inside the engine (`infraforge-engine`)**: Embedding the window and renderer loops into the main engine process.
2. **Offscreen rendering via shared memory / texture sharing**: Rendering Vulkan to offscreen shared textures/buffers and displaying them inside Chromium via WebGL/WebGPU.
3. **Dedicated native viewport process (`infraforge-viewport`)**: Running the renderer in an isolated process parented as an OS child surface (Win32 child `HWND`) within the Electron window frame, managed via bidirectional stdio control.

Option 1 creates thread contention, couples engine headless/server capabilities to OS GUI libraries, and means a GPU driver crash or presentation deadlock crashes the canonical project database and network session.
Option 2 incurs severe latency, copy overhead, synchronization complexity, and color space/compositor double-buffering penalties at high resolutions.
Option 3 provides strict process isolation, native GPU presentation performance, independent crash recovery, and decouples the domain engine from platform UI handles.

## Decision

The Vulkan renderer runs as a dedicated native executable (`infraforge-viewport`), launched and supervised by Electron's `ViewportSupervisor`.

1. **Child Window Parenting**: The viewport process creates a native Win32 window configured with `WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS` and parents it into the host Electron window HWND.
2. **DPI Awareness**: The viewport enables per-monitor-v2 DPI awareness before creating any window class or surface (`SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`).
3. **Control Channel**: Communication between the desktop shell and the viewport process uses a line-oriented JSON protocol over standard input/output (`stdin`/`stdout`).
4. **Visibility & Overlay Suppression**: The desktop shell manages viewport placement and hides the child window behind blocking modal overlays (such as project creation dialogs) to prevent clipping artifacts.
5. **No Domain Mutation**: The viewport consumes scene description data only. It has no direct database or file mutation privileges.

## Consequences

- Electron supervises two child processes: `infraforge-engine` (over WebSocket) and `infraforge-viewport` (over stdio).
- A GPU crash or device-lost event in the viewport does not corrupt SQLite transactions or kill the engine session.
- Startup, placement, and shutdown semantics are governed by the Viewport Control Protocol (`docs/03_PROTOCOL/VIEWPORT_CONTROL_PROTOCOL.md`).
- Multi-platform support requires platform-specific surface wrappers (`SurfaceWin32.cpp`, and future Linux X11/Wayland integrations).
