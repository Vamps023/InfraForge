# Vulkan renderer architecture

The Vulkan renderer visualizes derived scene data inside the dedicated `infraforge-viewport` process. It consumes derived render descriptions (`RenderScene`) from the engine and does not own canonical domain state or query SQLite directly.

## Module structure (`viewport/`)

```text
viewport/
├── include/infraforge/viewport/
│   ├── ViewportApplication.hpp
│   ├── control/       ControlProtocol.hpp (stdio IPC commands/records)
│   ├── platform/      SurfaceFactory.hpp, NativeSurface.hpp
│   └── renderer/      VulkanRenderer.hpp, VulkanDevice.hpp, SwapchainState.hpp
├── src/
│   ├── control/       JSON parsing and serialization for stdio IPC
│   ├── platform/      Win32 HWND surface creation, per-monitor-v2 DPI awareness
│   └── renderer/      
│       ├── VulkanInstance.hpp/.cpp   (Vulkan 1.3 instance, Khronos validation)
│       ├── VulkanDevice.hpp/.cpp     (Physical/logical device, queues, command pools)
│       ├── VulkanSurface.hpp/.cpp    (Surface format/colorspace pair selection)
│       ├── VulkanSwapchain.hpp/.cpp  (Swapchain lifecycle, images, views, framebuffers)
│       ├── SwapchainState.hpp/.cpp   (Pure state machine for acquire/present handling)
│       ├── RenderThread.hpp          (Guaranteed join-after-stop rendering thread)
│       ├── GridPass.hpp/.cpp         (Embedded GLSL shaders via shaderc, staging upload)
│       ├── EditorCamera.hpp/.cpp     (Perspective/top Z-up editor camera)
│       └── SelectionId.hpp/.cpp      (Bitfield encoding for GPU picking)
└── tests/             Doctest suites (camera, control protocol, render thread, swapchain)
```

## Implemented baseline (Phase 4)

### 1. Swapchain lifecycle state machine

The renderer handles swapchain acquisition and presentation through an explicit state machine (`SwapchainState`):

- **`VK_ERROR_OUT_OF_DATE_KHR` on acquire**: Immediately recreates the swapchain before retrying.
- **`VK_SUBOPTIMAL_KHR` on acquire**: Completes the render and presentation of the current frame so the acquire semaphore is consumed, then schedules swapchain recreation for the subsequent frame.
- **`VK_TIMEOUT` / `VK_NOT_READY`**: Safely skips the frame without state corruption.
- **Zero extent**: When width or height drops to zero (e.g., shell minimized or hidden behind an overlay), rendering transitions to `suspended` and waits for non-zero bounds before recreating.
- **Fatal failures**: `VK_ERROR_DEVICE_LOST`, surface lost, or out-of-memory transitions the renderer to `device_lost` or `failed` state and emits an explicit status record to the shell.

### 2. Threading and synchronization

- **`RenderThread`**: Rendering executes on a dedicated thread decoupled from the OS message pump.
- **Join discipline**: Self-stopping render threads guarantee synchronous `join()` upon exit; detached threads are strictly forbidden.
- **Frame synchronization**: Per-frame semaphores (image acquired, render finished) and fences guarantee synchronization without GPU stalls or presentation races.

### 3. World grid and camera

- `EditorCamera` owns a double-precision project-global target, orbit distance,
  yaw around world Z, clamped pitch, vertical FOV, viewport aspect, dynamic
  near/far planes, and Perspective/Top projection state.
- Perspective projection uses Vulkan's zero-to-one depth convention and
  framebuffer-Y correction. Top mode is orthographic and preserves focus.
- Coordinates are Easting/Northing/Up (Z-up). Render-origin subtraction happens
  in double precision before the GPU-facing float matrix is produced.
- Grid and terrain share the camera matrix and depth attachment.
- Shader compilation using `shaderc` from embedded GLSL sources with RAII staging buffers.
- Per-monitor-v2 DPI awareness enabled before window creation to ensure 1:1 physical pixel presentation.

### 4. Terrain pass and streaming (issue #6)

- **Depth attachments**: the swapchain owns per-image depth buffers; the
  render pass clears (color, depth) and both passes draw inside it —
  terrain resolves terrain/terrain and terrain/grid occlusion through depth
  testing.
- **Terrain pass** (`TerrainPass`): depth-tested heightfield pipeline from
  embedded GLSL; tile grid parameters are canonical doubles, converted
  through the shared `RenderLocalFrame` in double precision with the float
  reduction exactly at that boundary (northing flipped for the camera's
  Y-down convention). Per-vertex normals derive from height gradients;
  NoData quads are dropped (holes, not fabricated surfaces); vertical edge
  skirts hide LOD seams.
- **Streaming policy** (`TerrainTileCache`): GPU-independent residency
  state machine over the world vocabulary (unloaded → loading → resident;
  stale on dataset-revision drift; evicting → unloaded), deterministic
  camera-metric LOD (`floor(log2(mpp / level0Spacing))` clamped to the
  pyramid), bounded working set (≤ 64 resident tiles, farthest-first
  eviction, ≤ 2 loads per frame). Tile decode validates the embedded
  provenance (dataset id + revision) against the scene manifest — stale
  files are rejected, never re-shown.
- **Scene input**: the engine's `terrain.get_scene` projection reaches the
  viewport as a `scene` control command through the desktop shell; the
  renderer never queries SQLite and the shell/frontend never interpret
  tile payloads.
- **Camera input**: typed pan/orbit/dolly/action events feed the render-thread
  camera through a bounded queue. The first non-empty scene frames once if the
  user has not moved; scene refresh and resize preserve the established pose.

---

## Planned capabilities (WIP / future milestone implementation)

The following renderer components are designed but remain Work In Progress (WIP) as subsequent product phases unlock:

### Terrain appearance (later phases)
- Elevation texturing and slope-dependent material shading on top of the
  implemented terrain geometry pass.

### Road & infrastructure passes (Phases 7–8)
- Continuous road ribbon mesh generation from geometric alignments.
- Surface markings pass with stencil/depth offset resolution.
- Opaque PBR pipeline with material instancing for traffic furniture and buildings.

### Picking & selection readback (Phases 7+)
- Offscreen integer ID render target encoding stable domain selection IDs (`SelectionId`).
- Asynchronous fence-guarded pixel readback to resolve mouse clicks back to canonical entities.

### Large-world streaming (Phase 5+)
- Hierarchical frustum/occlusion culling.
- GPU resource streaming bounded by active spatial chunk residency.
