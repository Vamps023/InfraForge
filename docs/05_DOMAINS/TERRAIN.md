# Domain: Terrain

**Status:** Implemented end-to-end (engine, protocol, persistence, viewport
rendering); verified by doctest suites, the extended engine self-check, and
the protocol smoke (see `docs/IMPLEMENTATION_STATUS.md`).

The Terrain domain owns canonical elevation datasets imported from real
georeferenced rasters (GeoTIFF via GDAL). It is the first authoring-scale
domain built on the World partition (issue #4) and the Geo service's single
canonical georeference (ADR-0007).

## Canonical source of truth

- **Canonical record** — `terrain_datasets` rows in `project.db`
  (migration 3): stable 128-bit dataset id (UUID text primary key, also the
  world-index `EntityId`), display name, source CRS/geometry, elevation
  unit + metre factor, NoData flag/value, canonical coverage bounds and
  elevation range (project linear unit), SHA-256 + byte size of the stored
  raster, per-dataset revision, import diagnostics, timestamps.
- **Project-owned raster storage** — `terrain/elevation/<uuid>.tif` inside
  the `.iforge` directory. The import *copies* the source file (streamed,
  hashed while copying), so the dataset survives source-file disappearance.
  Canonical references are project-relative paths only (relocatable
  projects; ADR-0005).
- **Derived, rebuildable** — `cache/terrain/<uuid>/tile_x_y.iforgetile`
  files are never canonical; deleting `cache/` costs rebuild time only.

## Import pipeline (cancellable job)

`terrain.import_dataset` runs as a `terrain.import` job on the engine's
serial worker (THREADING_MODEL.md):

1. **Probe** (executor): GDAL opens the source read-only. Validation is
   explicit — readable raster, supported numeric sample type, positive
   dimensions, north-up non-rotated geotransform, CRS present and
   resolvable (a missing CRS is a typed failure, never an assumption),
   known elevation unit (absent band unit = metre, the documented DEM
   convention; unknown units fail).
2. **Copy + hash** (worker): stream the source into
   `terrain/elevation/<uuid>.tif.importing` in bounded chunks; progress
   units are real bytes; SHA-256 computed while copying.
3. **Validate stored copy** (worker): re-probe the copied file; a copy that
   does not match the probe is corrupt.
4. **Coverage + range** (worker, worker-confined `GeoTransformService` —
   PROJ objects are thread-bound): the four source coverage corners
   transform to canonical project-global space (double precision, strict
   non-ballpark operations); non-finite results fail the import. The
   elevation range scans the raster in bounded strips; NoData cells are
   counted (surfaced as a `nodata_cells` diagnostic) and never resampled.
5. **Commit** (executor, transactional): publish the raster at its
   canonical storage path, insert the `terrain_datasets` row (SQLite
   transaction; project revision +1; session dirty), register the dataset
   in the world `SpatialIndex` under `InvalidationClass::Terrain`. If the
   commit fails, the published raster is removed (compensation), no
   canonical row exists, and the job is marked FAILED.
6. **Tiles** (worker): a follow-up `terrain.tiles` job generates derived
   tiles for exactly the canonical chunk diff of the registration insert.
   Eager generation is bounded (`kMaxEagerTilesPerImport`); beyond that,
   `terrain.regenerate_tiles` covers the remainder in cancellable batches.

The import job uses `requiresFinalization=true`: the JobSystem keeps the
public state as `Running` until the executor-side commit calls
`markCompleted()` (success) or `markFailed()` (failure). The job is
`COMPLETED` only after all canonical side effects succeed; a commit
failure makes the authoritative job state `FAILED`, not `Completed`.

Cancellation is cooperative at bounded checkpoints (per chunk, per scan
strip, per tile). A cancelled or failed import leaves **no** canonical
state: the temp file is removed, no row exists, no events pretend success.
A project close cancels tracked jobs; a late completion re-verifies the
project identity before committing.

## CRS flow

All source↔canonical conversion goes through `GeoTransformService`
(ADR-0007) — terrain never calls PROJ directly, never invents local
coordinate math, and never reduces to float before the render boundary.
Dataset coverage is stored in canonical project-global coordinates;
changing the project georeference while terrain datasets exist is rejected
explicitly (stored canonical bounds would be silently invalidated).

## Canonical sampling (`terrain.sample` / `TerrainSampler`)

Double-precision, project-global in, canonical-linear-unit out:

- Bilinear interpolation over the four surrounding **cell centers**
  (cell-center grid semantics; edge samples clamp to the border stencil).
- Any NoData cell in the stencil ⇒ `NoData` result — no substitution, no
  nearest-valid search.
- Outside the closed raster footprint ⇒ `OutsideCoverage`.
- Height conversion: `z_source · unitToMetre / linearUnit.toMetre`, the
  same rule tile generation applies.
- Overlap rule (no dataset id given): the most recently imported covering
  dataset wins — deterministic.
- Sampling never reads Vulkan meshes; the renderer is not a height source.

## Tiling and the world partition

Tiles are chunk-aligned: one tile per world chunk a dataset covers, clipped
to the dataset's canonical bounds intersection. Each tile file is a
self-describing LOD pyramid (5 levels, 129→9 vertices per side):

```
magic "IFGT" | schemaVersion | generatorRevision | datasetUuid (36B text)
datasetRevision | chunkX | chunkY | lodCount | reserved
per LOD: dim | originE/N | cellE/N | minZ/maxZ | dim*dim float64 heights
```

Heights are absolute canonical elevations; NoData positions are quiet NaN.
Files publish atomically (temp + rename). The decoder validates magic,
schema version, LOD structure, and positive cell sizes — a stale or
corrupt tile is rejected, never interpreted leniently. Provenance
(dataset id + revision) is embedded and checked again by the renderer
against the scene manifest: stale results cannot overwrite or impersonate
newer revisions.

Tile generation samples the stored raster through one padded block read
per tile (block/tile reads, not per-vertex re-reads); vertices that fall
outside the pre-read window under strongly non-affine transforms resolve
through an exact per-point fallback.

## Renderer integration

`terrain.get_scene` projects the derived scene (metadata only): render
origin (project origin), per-tile chunk ids, revisions, absolute cache
paths, canonical coverage. The desktop shell forwards it verbatim to the
viewport process as a `scene` control command; the frontend never
interprets tile payloads and the renderer never queries SQLite.

The viewport's `TerrainTileCache` implements GPU-independent streaming
policy over the world residency vocabulary (unloaded → loading →
resident, stale on revision drift, evicting → unloaded, plus
`LodReplacing` for in-place LOD changes), a deterministic LOD rule
(`level = clamp(floor(log2(mpp / level0Spacing)), 0, 4)`), a bounded
working set (≤ 64 resident tiles, farthest-first eviction, ≤ 2 loads
per frame). A resident tile whose desired LOD changes enters
`LodReplacing`: the old GPU payload stays drawable until the new LOD
is successfully loaded, then the replacement is atomic. A failed
replacement retains the old resident payload rather than dropping
working terrain. `TerrainPass` renders depth-tested heightfields: the
double→float conversion happens exactly at the shared
`RenderLocalFrame` boundary, per-vertex normals come from height
gradients, NoData quads are dropped (holes, not fabricated surfaces),
and vertical edge skirts hide LOD seams. GPU buffer uploads use a
portable staging-buffer path (host-visible staging → device-local
vertex/index buffers via `vkCmdCopyBuffer`), not requiring
`HOST_VISIBLE | DEVICE_LOCAL` memory; non-coherent staging memory is
flushed explicitly. Native mouse input (wheel zoom, drag pan) drives
the render-thread camera; the first non-empty scene frames the camera
onto the terrain extent.

## Diagnostics

Typed `TerrainErrorCode` taxonomy surfaced as
`COMMAND_ERROR_CODE_TERRAIN_UNSUPPORTED`: missing CRS, corrupt source
(truncated TIFFs are rejected by a bounded structural pre-check before
libtiff can crash on them), unsupported raster configuration, invalid
coverage, NoData presence (informational), missing project-owned storage
(detected at reopen), failed tile generation. Failures identify the
dataset where possible; nothing is fabricated.

## Threading and consistency

Canonical mutations (commit, registry replay) run only on the application
executor; workers use immutable input snapshots and worker-confined
transform services; worker results carry their payload to the executor and
are version/identity-checked before commit (see `JobSystem`). Partial
failed imports never leave the project in a falsely valid state.

## Limitations

- One import job at a time (serial worker) — a queue, not a pool.
- Eager tile generation is bounded per import; very large coverages need
  explicit `terrain.regenerate_tiles` batches.
- Editing elevation in-place (re-import/replace) and dataset removal are
  not implemented yet; the surface today is import + query + regenerate.
- Overlapping datasets resolve by recency for default sampling; explicit
  dataset ids pin the source.
- Viewport camera control is orthographic top-down pan/zoom; a 3D orbit
  camera arrives with later viewport work.
