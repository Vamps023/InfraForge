# Viewport Control Protocol

This specification defines the inter-process communication protocol between the Desktop Shell (`apps/desktop/src/ViewportSupervisor.ts`) and the native viewport process (`viewport/` — `infraforge-viewport`).

## Transport and framing

- **Transport**: Standard input (`stdin`) and standard output (`stdout`).
- **Framing**: Line-delimited UTF-8 strings. Each control command is a single line terminated with `\n`.
- **Direction**:
  - `stdout` (Viewport -> Desktop Shell): Startup readiness records, status notifications, and log lines.
  - `stdin` (Desktop Shell -> Viewport): Inbound placement, visibility, and shutdown commands.

---

## 1. Process launch and CLI arguments

The desktop shell spawns `infraforge-viewport` passing the host window handle and initial geometry:

```bash
infraforge-viewport \
  --parent-window <hex-hwnd> \
  --screen-x <px> \
  --screen-y <px> \
  --width <px> \
  --height <px> \
  --dpi-scale <milli-percent> \
  [--validate]
```

- `--parent-window`: Hexadecimal string representation of the host OS window handle (e.g. `0x000204A8`).
- `--screen-x`, `--screen-y`: Physical screen coordinates of the viewport client origin.
- `--width`, `--height`: Physical width and height of the viewport surface in pixels.
- `--dpi-scale`: Integer milli-percentage of DPI scale (e.g. `1000` = 100%, `1250` = 125%, `1500` = 150%).
- `--validate`: Optional flag to enable Khronos Vulkan validation layers when available.

The native child window is **always created hidden** (omitting `WS_VISIBLE`), ensuring no startup timing or modal overlay race condition can cause the window to flash before its first valid placement. The first show occurs strictly through the desktop shell's post-readiness policy application (`place` -> `visibility visible:true`).

---

## 2. Handshake and status messages (Viewport -> Shell)

### 2.1 Readiness record

Once the native window is created and parented, the viewport process emits a single readiness line to stdout:

```text
INFRAFORGE_VIEWPORT_READY {"width":1920,"height":1080,"dpiScale":1.0,"platform":"win32"}
```

The desktop supervisor enforces a 10-second startup timeout. If the readiness record is not received within this window, the supervisor terminates the child process and marks the viewport state as `failed`.

### 2.2 Status record

Whenever the Vulkan renderer state transitions, the viewport process emits a status record to stdout:

```text
INFRAFORGE_VIEWPORT_STATUS {"state":"ready","detail":"Renderer initialized","validation":true,"gpu":"NVIDIA GeForce RTX 3060","vulkan":"1.3.280"}
```

#### Supported states:

| State | Description |
|---|---|
| `starting` | Viewport process launched; Vulkan instance/device initialization in progress. |
| `ready` | Device, swapchain, and pipelines initialized; normal frame rendering active. |
| `suspended` | Swapchain extent is zero (e.g., window minimized or viewport hidden). Render loop paused. |
| `recreating` | Swapchain out-of-date or suboptimal; recreating swapchain resources. |
| `device_lost` | Vulkan returned `VK_ERROR_DEVICE_LOST`. Unrecoverable for current session. |
| `failed` | Fatal initialization, shader compilation, or platform error occurred. |
| `stopped` | Viewport process has stopped or exited cleanly. |

---

## 3. Inbound control commands (Shell -> Viewport)

All commands sent by the desktop shell to `stdin` are formatted as single-line JSON objects with a required `"type"` field.

### 3.1 `place`

Reposition and resize the native surface following window moves, panel resizes, or monitor changes:

```json
{"type":"place","screenX":240,"screenY":80,"width":1440,"height":900,"dpiScale":1.25}
```

- Coordinates are physical screen pixels converted using `screen.dipToScreenRect` to ensure accuracy on multi-monitor mixed-DPI setups.
- If width or height is zero, the renderer suspends swapchain presentation without failing.

### 3.2 `visibility`

Show or hide the child surface:

```json
{"type":"visibility","visible":true}
```

The desktop shell manages a centralized visibility policy (`ViewportVisibilityPolicy.ts`). The surface is hidden when:
- The desktop window is minimized or hidden.
- A blocking modal dialog or application overlay is open (e.g., New Project dialog, Georeference settings).
- The viewport tab/dock is hidden.

When restored, placement is recomputed from the latest cached bounds before showing.

### 3.3 `scene`

Replaces the renderer's terrain scene manifest (engine-derived via `terrain.get_scene`, forwarded by the shell; opaque transport metadata for the shell):

```json
{"type":"scene","originEasting":500000.0,"originNorthing":4650000.0,"originHeight":0.0,
 "missingTiles":0,"revision":7,
 "tiles":[{"datasetUuid":"…","datasetRevision":3,"chunkX":-1,"chunkY":2,
           "path":"D:/proj.iforge/cache/terrain/<uuid>/tile_-1_2.iforgetile",
           "minE":499000.0,"minN":4649000.0,"maxE":500000.0,"maxN":4650000.0}]}
```

- `tiles: []` clears the terrain scene (no terrain is rendered).
- Tile paths are session-context absolute paths of derived, versioned cache
  files; the viewport validates each file's embedded provenance against
  this manifest and rejects stale/corrupt tiles explicitly.
- Malformed scenes are rejected with a parse error — never partially applied.

### 3.4 `shutdown`

Instruct the viewport process to cleanly exit:

```json
{"type":"shutdown"}
```

Upon receiving `shutdown`, the viewport application stops the render thread (guaranteeing join), destroys Vulkan swapchain and device resources, unregisters the Win32 window class, and exits with code 0. If the process does not terminate within a 2-second grace period, the supervisor force-kills the process.

### 3.5 `camera`

Issues a presentation-only camera action:

```json
{"type":"camera","action":"focus-terrain","datasetUuid":"<uuid>"}
{"type":"camera","action":"frame-all"}
{"type":"camera","action":"perspective"}
{"type":"camera","action":"top"}
```

`focus-terrain` requires a canonical selected dataset UUID. Camera math remains
inside the native viewport; the desktop shell only validates and forwards the
action.
