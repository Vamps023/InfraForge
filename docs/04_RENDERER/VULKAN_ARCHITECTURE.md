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
│       ├── GridCamera.hpp/.cpp       (Orthographic grid projection with DPI scaling)
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

- Orthographic grid pass rendering distance-scaled coordinate grids using double-precision camera offsets.
- Shader compilation using `shaderc` from embedded GLSL sources with RAII staging buffers.
- Per-monitor-v2 DPI awareness enabled before window creation to ensure 1:1 physical pixel presentation.

---

## Planned capabilities (WIP / future milestone implementation)

The following renderer components are designed but remain Work In Progress (WIP) as subsequent product phases unlock:

### Terrain pass (Phase 6)
- Multi-resolution terrain heightmap mesh evaluation.
- Elevation texturing, slope-dependent shading, and tile residency streaming.

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