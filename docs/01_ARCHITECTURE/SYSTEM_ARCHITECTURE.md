# System architecture

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ Desktop process (Electron shell)                                            │
│ - OS window & application lifecycle                                        │
│ - EngineSupervisor (engine lifecycle, loopback port/token validation)       │
│ - ViewportSupervisor (infraforge-viewport lifecycle, stdio control, HWND)   │
│ - validated preload API & native directory dialogs                          │
└───────────────┬───────────────────────────────┬─────────────────────────────┘
                │                               │
                │ preload bridge                │ child HWND + stdio control
                v                               v
┌───────────────────────────────┐      ┌──────────────────────────────────────┐
│ React frontend (renderer)     │      │ infraforge-viewport process          │
│ - presentation & UI state     │      │ - native Vulkan 1.3 renderer         │
│ - transient layout & draft    │      │ - Win32 child surface integration    │
│ - command dispatch & events   │      │ - render thread & swapchain machine  │
└───────────────┬───────────────┘      │ - grid camera & selection ID pass    │
                │                      └──────────────────▲───────────────────┘
                │ WebSocket (authenticated)               │ scene updates
                v                                         │
┌─────────────────────────────────────────────────────────┴───────────────────┐
│ infraforge-engine process                                                   │
│ Transport -> Application -> Domain                                          │
│                │          │                                                 │
│                │          ├─ Project / Geo / Terrain / Road / etc.          │
│                │          └─ Simulation                                     │
│                ├─ Persistence adapters (SQLite + project.json)              │
│                ├─ Import/export adapters                                    │
│                └─ Render scene derivation & invalidation                    │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Ownership rule

There is exactly one canonical mutable project model per open project session in the native engine. Frontend caches are query results/projections and may be discarded/reloaded.

## Feature module rule

Each domain exposes a public application API. Cross-domain behavior is coordinated by application services or explicit shared domain services; modules must not reach into another module's persistence tables or private implementation files.

## Plugin direction

Future plugins integrate through versioned capability interfaces and commands/events. Direct access to internal mutable containers is not part of the plugin contract.