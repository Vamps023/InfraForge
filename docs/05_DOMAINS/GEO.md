# Domain: Geo

**Status:** Geo runtime verified (engine tests + protocol smoke); renderer
boundary API implemented; renderer consumption pending integration with
the viewport domain (issue #2).

The Geo domain owns the canonical project georeference and every coordinate
transformation that crosses a spatial boundary. See
`docs/02_DATA/GEOREFERENCE.md` for the data model and
`docs/ADR/0007-single-canonical-georeference.md` for the decision.

## Native modules

| Module | Role |
|---|---|
| `domain::geo::GeoreferenceConfig` (`GeoreferenceConfig.cpp`) | Persisted canonical config + shape validation (non-empty bounded strings, finite origin values). |
| `domain::geo::GeoTypes` (`GeoTypes.hpp`) | Shared value types: `GeoCoordinate`, `SourceSpatialReference`, `ProjectGlobalPosition`, `RenderLocalPosition`, `isFinite`, and the `GeoError` failure taxonomy. |
| `domain::geo::ProjectGeoreference` (`ProjectGeoreference.hpp`/`.cpp`) | Resolved runtime model: `CrsKind` + `crsKindName`, `ResolvedCrs`, `ResolvedUnit`, `VerticalReference`, `ProjectGeoreference`. |
| `domain::geo::GeoTransformService` (`GeoTransformService.cpp`) | PROJ-backed runtime: CRS resolution, unit resolution, source→project-global transforms, and render-local frame derivation. PROJ objects (context + `PJ*` transform cache) are confined to a service instance bound to the application executor thread — not shared across threads. |
| `domain::geo::RenderLocalFrame` (`RenderLocalFrame.hpp`) | Header-only shared conversion API consumed by the renderer boundary — double-precision origin-relative conversion; the renderer may cast to GPU float precision afterward. |
| `application::GeoService` (`application/GeoService.cpp`) | Use cases: `getGeoreference`, `setGeoreference` (expected-revision guard), `transformToProjectGlobal`; translates `GeoError`/`StoreError` into typed `CommandFailure`. |

`ProjectService::create` resolves the create-time georeference through the
same `GeoTransformService`, so a project can never be created with a CRS the
engine cannot transform.

## Protocol surface (proto minor 1.2)

- Commands: `geo.get_georeference`, `geo.set_georeference`,
  `geo.transform_to_project_global` (`contracts/proto/.../v1/geo.proto`,
  routed in `CommandProcessor`).
- Results: `GeoreferenceStateResult`, `TransformToProjectGlobalResult`.
- Event: `georeference_changed` carrying the resolved `GeoreferenceInfo`
  and the new project revision.
- `COMMAND_ERROR_CODE_GEO_UNSUPPORTED` (code 9) reports unsupported
  CRS/transform requests distinctly from malformed input.

## Persistence

- Schema migration 2 adds `georeference.origin_height` (forward-only).
- `ProjectStore::updateGeoreference` rewrites the `georeference` row plus
  `project.json` and bumps the revision in one call boundary.
- `SqliteProjectStore` probes the live schema for `origin_height`, so test
  stores pinned to older migration lists keep consistent read/write shape.

## Renderer boundary

The renderer does not own or copy the canonical georeference. It receives
project-global coordinates and converts through the shared
`RenderLocalFrame` API — exact double-precision origin-relative
arithmetic. Float reduction to GPU precision happens only afterward, at
the renderer/GPU boundary.

## Strictness and consistency policy

- Coordinate operations are created with `ALLOW_BALLPARK=NO` and
  `ONLY_BEST=YES`; when the best transformation cannot be instantiated
  (missing grids, no operation between the CRS pair) the command fails with
  `COMMAND_ERROR_CODE_GEO_UNSUPPORTED` rather than serving degraded
  coordinates.
- `project.open` revalidates the persisted canonical georeference through
  `GeoTransformService`; an unresolvable or unsupported CRS leaves the
  store closed and reports the typed failure — the engine never activates a
  session on a broken frame.
- `geo.set_georeference` writes the manifest before committing the database
  transaction; on open, a manifest/database georeference divergence is
  repaired deterministically (the database row is canonical, the manifest
  is rewritten, the repair is logged).

## Verification

- `engine/tests/GeoTransformServiceTests.cpp` — resolution, unit handling,
  control-point transform (EPSG:4326 → EPSG:32633 at lon 15°/lat 55° ≈
  500000 E / 6094791.42 N), axis-order handling, origin/scale math, invalid
  and unsupported CRS rejection, wrong-role source CRS rejection
  (vertical/geocentric/compound as source horizontal; geographic/projected
  as source vertical), geocentric kind mapping, strict
  transformation selection (missing-grid and no-operation pairs),
  vertical axis-unit conversion both directions (ftUS vertical CRS,
  US-survey-foot linear unit), and compound transforms built via
  `proj_create_compound_crs` — authority + WKT2 definitions, resolved
  vertical-identity comparison across identifier syntaxes, and explicit
  failure for grid-dependent datum pairs.
- `engine/tests/GeoPersistenceTests.cpp` — create/reopen round-trip,
  update + revision bump, v1→v2 migration defaulting, manifest/DB
  georeference divergence repair, and manifest identity-corruption
  rejection.
- `engine/tests/ProjectServiceTests.cpp` — open-time revalidation of
  persisted invalid/unsupported CRS leaves no active session.
- `engine/tests/GeoServiceTests.cpp`, `CommandProcessorTests.cpp` — typed
  failure paths and event emission.
- `tools/engine-smoke/smoke.ts` — real-engine protocol verification of the
  full geo lifecycle including persistence across reopen.
- `infraforge-engine --self-check` runs a transform control-point assertion
  at startup so a broken PROJ deployment fails loudly.

## Limitations

- Inverse transforms (project global → arbitrary source CRS) exist in the
  domain service but are not exposed on the wire yet; importers currently
  only need the forward direction.
- Vertical datum transforms require both sides to carry resolvable
  vertical CRSs; without them, heights convert by axis unit only (no datum
  shift).
- PROJ search paths are resolved per-process at service construction; the
  installed share directory layout is covered by the compiled-in fallback.
