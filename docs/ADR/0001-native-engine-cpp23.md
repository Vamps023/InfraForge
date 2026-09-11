# ADR-0001: Native engine uses C++23

- Status: Accepted
- Date: 2026-09-11

## Decision

InfraForge's canonical engine/application/domain runtime is implemented in C++23.

## Rationale

The product requires direct native integration with Vulkan, predictable ownership/performance for large-world processing, access to mature GIS/native libraries, and one language boundary across domain/renderer-heavy systems.

## Consequences

- Domain code remains independent from Vulkan/platform/UI libraries.
- RAII and explicit ownership are required.
- CMake is the native build system.
- Cross-process UI communication occurs through the versioned protocol rather than native bindings into React.