# Test strategy

InfraForge verifies behavior at the lowest useful layer plus critical production paths. Every feature must be verified against real components rather than mock placeholders.

## Test layers

### 1. Native unit and domain tests (active & verified)
- **Engine test suite** (`engine/tests/`): Pure algorithms, coordinate math (PROJ), domain services (`ProjectService`, `GeoTransformService`), and persistence (`SqliteProjectStore`, migration v1→v2).
- **Viewport test suite** (`viewport/tests/`): Vulkan swapchain state machine, `RenderThread` join discipline, camera projection, control protocol JSON parsing, and DPI awareness.

### 2. Protocol and startup self-checks (active & verified)
- **Engine self-check** (`infraforge-engine --self-check`): Validates startup capabilities, SQLite transactions, PROJ georeferencing, and protocol baseline.
- **Protocol smoke test** (`tools/engine-smoke/`): Launches the real compiled engine and exercises authentication, `project.create`, `project.get_summary`, `geo.set_georeference`, save, close, and reopen over authenticated loopback WebSocket.

### 3. Frontend and desktop unit tests (active & verified)
- **Frontend test suite** (`apps/frontend/`): Vitest component tests covering project modals, CRS preset selection, and UI-only store projections.
- **Desktop supervisor tests** (`apps/desktop/`): Vitest process lifecycle tests covering `ViewportSupervisor` launch, readiness timeouts, and visibility suppression policies.

### 4. Golden project acceptance (WIP / future phases)
- Small deterministic projects exercising complete vertical workflows and round-trip persistence, import, and export. Golden fixtures are versioned and migrated like real projects.

### 5. Performance regression budgets (WIP / future phases)
- Measured workloads covering project open latency, spatial queries, single-entity edits, chunk rebuild times, renderer frame budgets (60 FPS on RTX 3060), and 60 Hz simulation load.

---

## Test execution commands

| Test Target | Scope | Execution Command |
|---|---|---|
| **Native doctest suites** | Engine & Viewport C++ tests | `ctest --test-dir build --build-config Release --output-on-failure` |
| **Engine startup check** | In-process DB & PROJ self-check | `.\build\engine\Release\infraforge-engine.exe --self-check` (Windows)<br>`./build/engine/infraforge-engine --self-check` (Linux) |
| **Engine protocol smoke** | Real engine loopback WebSocket | `npm run verify:engine` (requires `INFRAFORGE_ENGINE_PATH`) |
| **Frontend component tests** | React UI & preset logic | `npm --workspace @infraforge/frontend run test` |
| **Desktop supervisor tests** | Process supervision & visibility | `npm --workspace @infraforge/desktop run test` |
| **Full TypeScript check** | Strict compilation across all packages | `npm run typecheck` |
| **Protocol schema lint** | Buf contract validation | `npm run proto:lint` |

---

## Test honesty

A test using mocks proves only the mocked boundary contract. Runtime/GPU/packaging behavior is not claimed verified without executing the relevant runtime scenario. See `docs/IMPLEMENTATION_STATUS.md` for the authoritative ledger of verified capabilities.