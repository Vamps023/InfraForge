# InfraForge Foundation Code Review

## Scope and conclusion

This review covers the repository foundation as of 13 September 2026: native engine, persistence, geospatial runtime, authenticated protocol, Electron supervision, React shell, native viewport, Vulkan renderer, tests, and the approved architecture documents. Roads, lanes, junctions, terrain, world partitioning, simulation, and import/export are design-only at this revision.

The architecture is a good base for future authoring. Canonical state remains in the C++ engine, the frontend is a projection, geospatial transforms are centralized, persistence is transactional, and the native viewport has real Windows lifecycle coverage. The foundation is not release-ready for road authoring until the runtime defects and concurrency gaps below are closed. The most urgent defects are:

1. `VulkanSwapchain::create()` creates the render pass before selecting the surface format. If the selected surface format differs from the default, the render pass attachment format can disagree with the swapchain image format. Vulkan requires compatible render-pass and attachment formats; this must be fixed before relying on renderer portability.
2. `EngineSupervisor.waitForReady()` removes its child `exit` and stderr listeners after readiness. A later engine crash therefore leaves the supervisor reporting `ready`, and the stderr pipe is no longer drained. The shell can present a live engine that is gone and can eventually block the child on a full diagnostics pipe.
3. There is no project lock/lease or on-disk revision compare-and-swap. Two engine processes can open one project, each cache the same revision, and both accept edits. SQLite serializes writes, but it does not prevent stale writers from overwriting one another.

These are code findings, not inferred from specifications. The repository tests pass, but the test suite does not currently exercise the first two runtime failure modes or multi-process stale mutation.

## Evidence reviewed

The review read the required repository documents and accepted ADRs, then traced the real paths through `apps/frontend`, `apps/desktop`, `contracts`, `engine`, `viewport`, persistence, and tests. Verification executed during this review:

| Check | Result |
|---|---|
| Frontend and desktop TypeScript typecheck | Passed |
| Frontend component tests | 28 passed |
| Desktop supervisor and viewport policy tests | 13 passed |
| Protocol lint and TypeScript generation | Passed |
| Frontend and desktop production build | Passed |
| Native CMake Release build | Passed |
| Native CTest | 2 test targets passed |
| Engine self-check | Passed |
| Real authenticated engine protocol smoke | Passed |

Passing these checks proves the covered behavior only. It does not prove packaged Electron recovery, multi-process project ownership, or Vulkan behavior on a surface that reports a non-default format.

## What is correct today

The dependency direction matches the TRD and ADRs. Network callbacks post commands to a single application executor; domain code has no React, Electron, SQLite, or Vulkan dependency; the renderer consumes derived data; and the frontend receives results and events rather than mutating a duplicate project graph.

Project creation, open, save, save-as, close, schema migration, manifest validation, and georeference persistence follow a coherent path. The database is authoritative for schema and canonical georeference state. Newer schemas are probed read-only and rejected without modification. Georeference updates validate and canonicalize before writing, use an expected revision guard in the application service, and compensate a manifest write when the database transaction fails.

The PROJ integration is unusually explicit about failure. It resolves CRS and units, rejects unsupported CRS roles, disables ballpark transformations, and reports unavailable transformations rather than substituting coordinates. The render-local boundary preserves double precision until the GPU boundary.

The viewport has real Windows child-window hosting, hidden-first creation, placement on mixed-DPI displays, visibility policy for blocking overlays, and render-thread join discipline. The test evidence is meaningful for those policies. The current Vulkan implementation is still only a grid pass: it has no render-scene ingestion, road geometry, selection-to-domain resolution, chunk residency, or GPU resource invalidation path.

## Findings

### F1 — High: render-pass format is selected too late

In `viewport/src/renderer/VulkanSwapchain.cpp`, `create()` calls `createRenderPass()` while `format_` still has its default value, then queries surface formats and assigns `format_` from `selectSurfaceFormat()`. The render pass attachment therefore describes the default format even when the surface reports another supported `(format, colorspace)` pair. The framebuffer is created with the selected image view while referencing that render pass. Vulkan's framebuffer validity rules require the attachment to be compatible with the render pass attachment description.

Fix by querying and selecting the surface format before creating the render pass. Recreate the render pass and dependent pipelines whenever the format changes; do not assume that a swapchain recreation preserves the attachment format. Add a test seam for format selection and a validation-enabled runtime case using a deliberately non-default format where available.

### F2 — High: engine crash after readiness is invisible to the shell

`EngineSupervisor.waitForReady()` installs `child.once('exit', onExit)` and a stderr data listener, but `cleanup()` removes both as soon as readiness is parsed. `start()` then returns with `bootstrap.state = 'ready'`. If the child exits later, no code changes the bootstrap state, clears `this.child`, or notifies the renderer. The frontend can continue sending commands to a dead endpoint until each command times out. The detached stderr pipe is also not drained after readiness.

Keep a permanent exit/error watcher after readiness, separate startup parsing from lifetime monitoring, and transition the snapshot to `failed` or `disconnected` with the exit code and captured diagnostics. Continue draining stderr into bounded structured logs. Make `stop()` distinguish intentional shutdown from unexpected exit. Add tests that start a real child, kill it after readiness, and assert the state transition and restart behavior.

### F3 — High: multi-process stale writers are accepted

The project store documents one active session per store instance, but the product permits multiple engine processes. There is no project lock file, lease, process identity, or database compare-and-swap on mutation. A probe against two real engine processes showed both could open the same project. Each accepted a georeference update using `expected_revision = 1`; both returned success at revision 2 even though the second writer had observed stale state. Reopening the project showed the later write at revision 3.

Add an exclusive project lock/lease acquired during open and released during clean close, with stale-owner recovery rules. Keep an application-level expected revision check, and add a persistence-level `UPDATE ... WHERE revision = expected` guard for every mutation. Return a distinct conflict error with current revision. Test two real engine processes, crash recovery, lock contention, and read-only inspection of a locked project.

### F4 — Medium: protocol and implementation disagree on cancellation

`docs/03_PROTOCOL/WEBSOCKET_PROTOCOL.md` lists `cancel`, cancellable jobs, and job events, but the current foundation schema and command processor expose no cancellation command or job state. This is acceptable as a planned capability, but the protocol document reads as if it is available. Mark it planned in the protocol documentation, or implement the full route before relying on it for imports, mesh generation, or rebuilds.

### F5 — Medium: renderer and geo integration stops at the boundary

The Geo domain exposes `RenderLocalFrame`, while the status ledger says renderer consumption is pending. There is no immutable `RenderScene` service, scene-delta protocol, entity-to-selection registry owned by the application, or dirty-region subscription. Road editing cannot be added safely by sending mesh blobs from React or by letting Vulkan query SQLite. The next phase must implement this boundary before road mesh work.

### F6 — Medium: frontend reconnection and projection resynchronization are incomplete

The frontend rejects pending commands on socket close and displays a disconnected status, but it does not automatically reconnect or perform a revisioned snapshot resynchronization. Once road edits exist, reconnecting with an old projection can display stale lanes or selection state. Define a session epoch, snapshot query, event sequence/replay policy, and explicit invalidation of stale selection and operation state.

### F7 — Medium: the current renderer is not a large-world renderer

The grid pass is a fixed ±200 m CPU-built vertex buffer. The approved world-partition design is not implemented, and there is no camera-driven working set, chunk state machine, dirty dependency graph, or content-versioned mesh cache. This is not a defect in the grid milestone, but it is a hard prerequisite for 100 km+ projects and incremental road edits.

### F8 — Low: runtime evidence is Windows-heavy

The native CI builds on Windows and Linux, but the viewport surface is explicitly unsupported on Linux and the mixed-DPI acceptance is still outstanding. Keep the limitation visible. Add a Windows matrix covering 100%, 125%, and 150% DPI transitions, monitor moves, maximize/restore, child reparenting, and engine/viewport crash recovery.

## Road and junction foundation contract

### 2D and 3D quality parity

The 2D map and 3D viewport must remain equal-quality views of one canonical world. They share entity IDs, revisions, selection resolution, validation diagnostics, undo/redo records, and affected-region invalidation. A control-point move, lane connection, junction override, or validation repair issued in either view must reach the same application command and produce the same persisted result. The 2D view may use a map projection and the 3D view may use a perspective camera, but neither view may own alternate road geometry, CRS logic, lane topology, or junction state.

Parity is a release gate. Every road and junction capability needs paired 2D and 3D acceptance cases for create, select, precise edit, split, connect, disconnect, lock/override, validation, undo/redo, save/reopen, and failed rebuild recovery. Visual differences are acceptable when caused by projection or camera; semantic differences, missing diagnostics, different snapping rules, or different revision behavior are defects. This decision is recorded in `docs/ADR/0011-2d-3d-quality-parity.md`.

The canonical road model should be a stable-UUID graph of reference-line geometry, profiles, lane sections, and semantic links. Store analytic geometry as the source of truth: line, circular arc, clothoid/spiral, and explicit parametric curves. Store elevation and superelevation profiles separately from plan geometry. Derived road surfaces, markings, collision meshes, and render chunks must carry the source revision and be rebuildable.

Junctions should be explicit topology, not inferred only from mesh intersections. A junction owns entry/exit approaches and lane-to-lane links; each link records source lane, target lane, connecting geometry, priority/right-of-way, and validation diagnostics. Keep overpasses distinct from at-grade intersections using stack level and overlap-group semantics. Preserve the ability to lock or override an automatically proposed junction while retaining its provenance.

Every edit must be a transaction that returns affected entity IDs, old/new bounds, topology changes, and invalidation classes. A control-point move should rebuild only the dependent road sections, junction connections, and chunks. Moving a connected endpoint should use explicit constraints for continuity and direction; invalid geometry must be rejected before canonical state changes.

The mesh pipeline should consume immutable snapshots and produce a versioned result. A stale mesh result must be discarded by source revision, never applied over a newer edit. Junction surface construction needs robust planar predicates, explicit boundary constraints, lane-link-aware conflict checks, and deterministic triangulation. A constrained triangulation approach with exact or filtered predicates is appropriate for hard junction boundaries; CGAL documents this separation between geometric traits, topology, insertion, and displacement.^1

The UX should support the interactions that RoadRunner has proven useful: direct control-point dragging, precise numeric editing, inserting and deleting points, explicit curve segments, automatic intersections with a visible override, and height-aware intersection formation. RoadRunner also documents that self-intersections and double crossings should be split into connected road segments, and that stack level/overlap group prevent unwanted at-grade junctions.^2 SUMO Netedit provides a useful connection-editing model: show possible, existing, and conflicting lane targets and require an explicit commit; its documentation also treats internal junction links as first-class network data.^3

## Ordered repair roadmap

1. **Stabilize process ownership.** Add permanent engine and viewport lifetime monitoring, bounded diagnostics, explicit crash states, project lock/lease, and persistence compare-and-swap.
2. **Define the mutation framework.** Add command envelopes with expected revision, undo/redo command records, transaction results, affected bounds, invalidation classes, and conflict diagnostics.
3. **Implement world partitioning.** Add chunk indexing independent of entity identity, dirty-region computation, derived cache versioning, working-set streaming, and eviction tests.
4. **Implement render-scene transport.** Add immutable scene snapshots/deltas, selection-ID resolution, render-thread upload queues, stale-result rejection, and device-loss recovery.
5. **Implement road geometry.** Add analytic reference lines and profiles, continuity constraints, snapping, control-point operations, deterministic sampling, and domain validation.
6. **Implement lane topology.** Add lane sections, width/marking profiles, permissions, lane identity independent of mesh, and road-end connection constraints.
7. **Implement junction topology and meshing.** Add explicit junctions, lane links, right-of-way, automatic proposals with locks/overrides, constrained triangulation, and local rebuilds.
8. **Add import/export and golden fixtures.** Round-trip OpenDRIVE 1.9.0, preserve source IDs separately, validate connection and geometry semantics, and use deterministic fixtures for road edits and junctions.
9. **Add acceptance evidence.** Verify control-point edits, connected endpoints, T and four-way junctions, overpasses, lane-link conflicts, undo/redo, crash recovery, chunk rebuild scope, and renderer validation on supported hardware.

## Definition of done for the road phase

A road feature is complete only when a user action travels through the generated protocol, application command, domain invariants, transactional persistence, invalidation computation, immutable render-scene update, viewport selection, and frontend projection. Tests must prove stale revisions are rejected, failed transactions leave canonical state unchanged, derived meshes can be deleted and rebuilt, and a local edit does not rebuild unrelated chunks. A design document or interface alone is not evidence of implementation.

## Sources

1. CGAL, “2D Triangulations,” version 6.2.1, sections on constrained triangulations and software design. https://doc.cgal.org/latest/Triangulation_2/index.html
2. MathWorks, “Road Plan Tool,” RoadRunner documentation. https://www.mathworks.com/help/roadrunner/ref/roadplantool.html
3. Eclipse SUMO, “editModesNetwork” and “SUMO Road Networks.” https://eclipse.dev/sumo/docs/Netedit/editModesNetwork.html and https://eclipse.dev/sumo/docs/Networks/SUMO_Road_Networks.html
4. ASAM, “OpenDRIVE 1.9.0.” https://www.asam.net/standards/detail/opendrive/
5. PROJ, “Functions,” options `ALLOW_BALLPARK` and `ONLY_BEST`. https://proj.org/en/stable/development/reference/functions.html
6. Vulkan Documentation Project, `VkFramebufferCreateInfo` valid usage. https://docs.vulkan.org/refpages/latest/refpages/source/VkFramebufferCreateInfo.html
