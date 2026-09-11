# System architecture

```text
┌──────────────────────────────────────────────────────────────┐
│ Desktop process                                             │
│ Electron                                                    │
│ - OS window lifecycle                                       │
│ - engine process lifecycle                                  │
│ - native viewport hosting                                   │
│ - secure preload                                            │
└───────────────┬──────────────────────────────┬───────────────┘
                │                              │
                │ validated preload API        │ native surface
                v                              v
┌───────────────────────────────┐      ┌──────────────────────┐
│ React frontend                │      │ Vulkan viewport      │
│ presentation + UI state       │      │ native renderer      │
└──────────────┬────────────────┘      └──────────▲───────────┘
               │ WebSocket                        │ scene deltas
               v                                  │
┌─────────────────────────────────────────────────┴────────────┐
│ infraforge-engine                                           │
│ Transport -> Application -> Domain                          │
│                │          │                                  │
│                │          ├─ Geo/Terrain/Road/etc.          │
│                │          └─ Simulation                      │
│                ├─ Persistence adapters                       │
│                ├─ Import/export adapters                     │
│                └─ Render scene derivation                    │
└──────────────────────────────────────────────────────────────┘
```

## Ownership rule

There is exactly one canonical mutable project model per open project session in the native engine. Frontend caches are query results/projections and may be discarded/reloaded.

## Feature module rule

Each domain exposes a public application API. Cross-domain behavior is coordinated by application services or explicit shared domain services; modules must not reach into another module's persistence tables or private implementation files.

## Plugin direction

Future plugins integrate through versioned capability interfaces and commands/events. Direct access to internal mutable containers is not part of the plugin contract.