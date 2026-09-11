# Vulkan renderer architecture

## Renderer boundary

The renderer consumes a derived `RenderScene`. It does not query SQLite directly and does not own road/terrain/infrastructure semantics.

## Module structure

```text
renderer/
├── core/          instance, device, queues, swapchain, sync
├── resources/     buffers, images, samplers, descriptors, pipelines
├── scene/         render entities, transforms, cameras, lights
├── passes/        depth, terrain, opaque/PBR, markings, selection, debug
├── picking/       GPU/CPU selection support
├── streaming/     residency/upload/eviction
└── platform/      native surface integration
```

## Baseline

The renderer targets a Vulkan 1.3-capable baseline. Newer Vulkan capabilities may be enabled after capability detection; they are not unconditional requirements unless the supported-hardware specification is explicitly revised.

## Resource ownership

- Vulkan objects have explicit RAII ownership.
- Destruction is deferred/synchronized according to GPU use.
- Upload staging lifetime is separated from persistent resources.
- Asset/cache IDs are not raw Vulkan handles.
- Device loss/initialization failures produce explicit renderer state/diagnostics.

## Large-world precision

Domain transforms are double precision. The renderer derives local/camera-relative float transforms. Origin changes do not rewrite canonical geometry.

## Draw scalability

The architecture permits instancing, indirect drawing, LOD, frustum/occlusion culling, texture/material residency, and chunk-based streaming. Concrete optimizations are enabled based on measured bottlenecks and hardware capabilities.

## Picking

Picking returns stable render/entity selection IDs that are resolved back to canonical domain IDs. Selection rendering is a derived visual state and does not alter entity identity.