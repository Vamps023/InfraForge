# Threading model

InfraForge uses explicit ownership and message/task boundaries. Shared mutable domain state is not accessed concurrently without an owning executor/transaction boundary.

## Native engine execution roles

- **Application/domain executor** — serializes project mutations and revision changes.
- **Network I/O** — accepts WebSocket connections and decodes/encodes transport frames; it posts validated commands to the application executor.
- **Render thread** — owns Vulkan queue/resource submission rules required by the renderer architecture.
- **Simulation worker** — advances fixed-step simulation from immutable/configured inputs and publishes snapshots/deltas through controlled synchronization.
- **Worker pool** — CPU-heavy cancellable work such as import parsing, terrain processing, mesh generation, asset conversion, and export preparation.

## Rules

1. Network callbacks never mutate domain entities directly.
2. Worker jobs never commit partial canonical state. They produce results that are validated and transactionally applied by the application layer.
3. Vulkan object lifetime follows explicit renderer ownership; domain entity deletion produces render invalidation, not direct cross-thread GPU destruction.
4. Simulation uses a fixed timestep. Rendering interpolates/presents simulation state independently.
5. Cancellation is cooperative and checked at bounded work intervals.
6. Shutdown joins owned workers; detached background tasks are forbidden for production work.
