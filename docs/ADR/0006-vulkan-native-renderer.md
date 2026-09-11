# ADR-0006: Native viewport renderer uses Vulkan

- Status: Accepted
- Date: 2026-09-11

## Decision

InfraForge's production 3D viewport uses a native Vulkan renderer with a Vulkan 1.3-capable baseline and runtime capability detection for newer features.

## Consequences

The renderer consumes derived render-scene state and cannot become the canonical road/terrain/project model. Native viewport lifecycle must be proven before major geometry authoring expands.