# OpenGeoStudio donor-port audit

This is an actionable code audit of the read-only donor repository at
`D:\git\OpenGeoStudio-Qt` against InfraForge. OpenGeoStudio is a donor for
algorithms and workflow lessons; InfraForge remains the architecture and
canonical-state authority. Decisions use these meanings: **PORT DIRECTLY**,
**ADAPT**, **MERGE WITH INFRAFORGE**, **KEEP INFRAFORGE**, and **IGNORE**.

## Terrain

| Area | OpenGeoStudio-Qt implementation | InfraForge implementation | Decision | Reason |
|---|---|---|---|---|
| Selected-area workflow | `src/state/terrainWorkflow.ts`, `src/components/layout/TerrainWorkflowPanel.tsx`, `src/engine/tileGrid.ts` | `apps/frontend/src/features/terrain/DownloadAreaMap.tsx`, `engine/src/application/TerrainService.cpp` | KEEP INFRAFORGE | InfraForge already separates transient map selection from native authoritative planning, validates plan identity, supports sparse selections, and deduplicates provider requests. The donor square-grid helper expands rectangular selections to a square grid and is unsuitable as canonical planning logic. Retain only the donor's simple staged-workflow UX cues. |
| XYZ tile range math | `electron/terrain/tileMath.cjs` | `engine/src/domain/terrain/TerrariumTerrainProvider.cpp` | KEEP INFRAFORGE | The donor includes exact east/south boundaries with `floor`, causing overfetch. InfraForge implements tested half-open boundary semantics, latitude validation, and overflow-safe ranges. |
| DEM acquisition/retry | `electron/terrain/demDownloader.cjs`, `electron/terrain/downloader.cjs` | `engine/src/domain/terrain/TerrainDownloadProvider.cpp`, `TerrariumTerrainProvider.cpp`, `MapboxRgbTerrainProvider.cpp`, `OpenTopoTerrainProvider.cpp` | KEEP INFRAFORGE | Donor acquisition runs in Electron, has string-classified errors, no cooperative cancellation, and substitutes a browser user-agent. InfraForge has native typed errors, bounded retry, interruptible cancellation, credentials boundaries, and canonical commit cleanup. |
| DEM decoding | `electron/terrain/demDownloader.cjs` (`geotiff`, `sharp`) | provider decoders plus `engine/src/persistence/GdalTerrainSource.cpp` | KEEP INFRAFORGE | GDAL provides the already-integrated, cross-platform authoritative raster path, preserves CRS/NoData/unit metadata, and avoids a second JavaScript raster implementation. |
| Canonical terrain storage | export files and frontend terrain state | `TerrainDataset`, SQLite migrations, project-owned raster storage | KEEP INFRAFORGE | Donor terrain is not integrated into one transactional canonical project model. InfraForge persists source provenance, coverage pieces, units, hashes, and offline-reopen state. |
| Height sampling | `native/terrain/TerrainChunk.cpp::sampleHeight` | `engine/src/domain/terrain/TerrainSampler.cpp` | KEEP INFRAFORGE | Donor sampling clamps out-of-bounds coordinates and returns `0` for missing data, silently fabricating height. InfraForge uses double precision, CRS transformation, explicit OutsideCoverage/NoData, unit conversion, and rejects corrupt non-finite samples. |
| Terrain chunk mesh | `native/terrain/TerrainChunk.cpp` | `TerrainTileGenerator.cpp`, `TerrainTileFile`, `viewport/src/renderer/TerrainScene.cpp` | MERGE WITH INFRAFORGE | InfraForge's chunk-aligned, self-describing LOD pyramid and sparse NoData holes are stronger. Donor skirt construction and its regression fixtures are candidates only if crack testing proves the current shared-edge LOD scheme insufficient. Do not port donor edge morphing as written: its morph distance is in grid indices but compared with world metres, treats height `0` as “no neighbor,” and can alter canonical edge appearance inconsistently. |
| Terrain normals | central differences in `TerrainChunk.cpp::computeNormal` | renderer mesh construction in `TerrainScene.cpp` | COMPARE/ADAPT | Donor central-difference normals are simple and useful as a fixture, but boundary normals use one-sided in-chunk samples and do not guarantee cross-chunk equality. Add paired-edge regression tests before adopting any formula. |
| Terrain LOD/streaming | `src/engine/chunkLod.ts`, `native/streaming/ChunkManager.cpp` | `TerrainTileCache.cpp`, `ChunkResidency.cpp` | KEEP INFRAFORGE | InfraForge has a bounded resident set, stale-version rejection, limited loads per update, and old-payload retention during LOD replacement. Donor TypeScript LOD is presentation-side and less complete. |
| Seam/crack handling | skirts and optional edge morph in `TerrainChunk.cpp` | identical chunk-edge samples from chunk-aligned LOD grids; Vulkan tile pass | MERGE WITH INFRAFORGE | Port the donor's seam-focused test intent, not its implementation. First prove exact shared-edge heights and mixed-LOD behavior in InfraForge; add skirts only as derived rendering if a real crack remains. |
| Imagery | `electron/terrain/imageryDownloader.cjs` | `EsriImageryProvider.cpp` and terrain source/provenance path | KEEP INFRAFORGE | Donor fills failed tiles with black, a silent fallback forbidden by InfraForge. InfraForge must keep typed failure and canonical coverage semantics. |
| Export | `electron/terrain/exportEngine.cjs`, `formatWriter.cjs`, `geotiff-writer.cjs` | `engine/src/application/TerrainExportEngine.cpp` | KEEP INFRAFORGE | InfraForge's native GDAL-backed formats, project CRS, and explicit errors are the maintained production path. |
| Vulkan terrain shaders | `native/shaders/terrain.vert`, `terrain.frag` | `shaders/terrain.vert.glsl`, `terrain.frag.glsl`, `TerrainPass.cpp` | KEEP INFRAFORGE | InfraForge shaders and pass are integrated with its render-local frame, streaming cache, contours, hillshade, and current vertex contract. |

## Roads

| Area | OpenGeoStudio-Qt implementation | InfraForge implementation | Decision | Reason |
|---|---|---|---|---|
| Canonical alignment | `native/core/alignment/Alignment3D.*`, `AlignmentSampler.*` | `engine/src/domain/road/AlignmentPrimitives.cpp`, `ReferenceAlignment.cpp` | KEEP INFRAFORGE | Both support line/arc/clothoid. InfraForge validates G0/G1/curvature continuity and evaluates clothoids with deterministic Gauss-Legendre quadrature. Donor clothoids use stepwise integration and expose spline/polyline as canonical segment types. |
| Source fitting | `native/road/RoadGeometry.cpp`, `src/engine/roadGeometry.ts` | `AlignmentFitter.cpp` | MERGE WITH INFRAFORGE | Preserve InfraForge protected anchors and typed diagnostics. Evaluate donor adaptive chord-error sampling and curve fixtures as improvements to derived tessellation, not as replacement canonical Bezier/polyline truth. |
| Adaptive alignment sampling | `AlignmentSampler.cpp::adaptiveStations`, `RoadGeometry.cpp::adaptiveStepForRadius` | error-bounded adaptive sampling in `RoadTessellation.cpp` | ADAPT — IMPLEMENTED | Donor curvature-aware sampling intent was adapted, while its heuristic thresholds were replaced by a 5 cm default world-space surface-deviation contract. Segment, elevation, banking, and width breakpoints are retained exactly; pathological refinement has a hard cross-section cap and regression coverage. |
| Vertical profile | `RoadGeometry.cpp`, `native/road/profile/RoadProfile.*`, polynomial segments in `Alignment3D` | `VerticalProfiles.cpp`, Road profile protocol/UI | MERGE WITH INFRAFORGE | Keep canonical InfraForge breakpoint persistence and commands. Donor cubic vertical/banking segment evaluation is a candidate for a future versioned profile primitive; it cannot be silently substituted for existing piecewise-linear truth. |
| Width/cross-section | `RoadProfile.h`, `LaneSectionEvaluator.cpp`, `LaneLayout.cpp` | `RoadWidthProfile`, road protocol/UI, SQLite migration 11, `RoadTessellation` | ADAPT — IMPLEMENTED (surface width) | Donor station-aware taper evaluation was adapted into an engine-owned left/right surface-width profile attached to canonical Road IDs, including protocol, persistence, undo/redo, refit preservation, tessellation, and tests. Lane-specific topology remains intentionally deferred until required by the road surface workflow. |
| Road mesh | `RoadMeshBuilder.cpp`, `RoadMeshCompiler.cpp` | `RoadTessellation.cpp`, `RoadService::roadSceneProjection`, `RoadPass.cpp` | MERGE WITH INFRAFORGE | Keep chunk-stable meshes, render-local conversion, fingerprinted GPU updates, and deferred buffer retirement. Adapt donor per-station lateral offsets, banking application, pavement strip, and marking algorithms after the canonical width model exists. |
| Banking/superelevation | `AlignmentSampler.cpp`, `RoadMeshBuilder.cpp` | canonical `SuperelevationProfile`, banked cross-section tessellation and normals | ADAPT — IMPLEMENTED | InfraForge keeps its persisted banking profile and now applies the angle through `tan(angle)` to asymmetric surface edges and derives matching Vulkan scene normals. Sign convention and edge-height/normal behavior have regression coverage. |
| Terrain interaction | `src/engine/roadSurfaceInfluence.ts` and frontend workflows | no canonical road-terrain conformance workflow | ADAPT | Reuse concepts only after canonical terrain sampling and road profile ownership are maintained. Conformance must be an explicit road command, not a renderer or frontend mutation. |
| OSM conversion | `native/import/osm/OsmRoadImporter.*`, `src/engine/osmRoads.ts` | fitter contract only | ADAPT | Donor tag mapping and import fixtures are reusable. Coordinates must enter through InfraForge Geo service; source evidence and protected topology anchors must persist separately from canonical alignment. |
| OpenDRIVE | `native/import/opendrive/*`, `src/engine/opendrive.ts` | pending | ADAPT | Parser/export mappings can seed adapters, but donor persistence and IDs cannot be imported as a parallel model. |
| Selection/picking | native road scene/picking and React overlays | native viewport picking, road commands, `roadToolStore.ts` | KEEP INFRAFORGE | InfraForge already routes stable IDs and canonical commands through viewport interactions without frontend geometry truth. Borrow only clearer donor affordances. |
| Incremental GPU updates | `RoadChunkManager`, `RoadGpuPool` | road chunk tessellation cache and `RoadPass` fingerprints/deferred retirement | KEEP INFRAFORGE | InfraForge already satisfies per-road/chunk invalidation and avoids `vkDeviceWaitIdle` during ordinary edits. |

## Dependencies and licensing

The donor uses JavaScript `geotiff`, `sharp`, and `proj4`, plus native Vulkan,
SQLite, and nlohmann-json. No donor dependency currently justifies adding a
second InfraForge production path: InfraForge already uses GDAL/PROJ for
raster/CRS correctness, SQLite for canonical persistence, and Vulkan for the
native renderer. Code copied from the donor remains project-owned code; any
third-party-derived fixture or algorithm must retain its upstream license and
attribution. No third-party source has been copied by this audit.

## Immediate port order

1. Strengthen Terrain shared-edge and mixed-LOD regression coverage using the
   donor seam cases as test inspiration. Keep InfraForge generation unless a
   failing fixture demonstrates a production gap.
2. Complete manual real-provider/Vulkan Terrain acceptance and record exact
   evidence; automated tests alone do not close the Terrain gate.
3. ~~Add an engine-owned Road cross-section/width model and apply canonical
   superelevation through protocol, persistence, undo/redo, chunk tessellation,
   and RoadPass.~~ Completed by `d2d9611` plus the adaptive tessellation work.
4. Adapt donor pavement-strip and marking concepts into the existing chunked
   RoadPass scene contract without introducing lane-domain ownership yet.
5. Add an explicit road-to-terrain conformance command, then adapt OSM import
   only after that canonical profile/mesh path is proven.
