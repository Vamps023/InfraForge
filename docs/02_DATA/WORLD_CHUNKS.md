# World partition and chunks

World partition limits processing and renderer residency for large projects. The domain foundation is implemented in `engine/src/domain/world/` (see `docs/05_DOMAINS/WORLD.md`); this document defines the data semantics.

## Canonical bounds

Spatial bounds are axis-aligned rectangles in **project-global space** — the canonical double-precision easting/northing frame of the Geo domain (ADR-0007), expressed in the canonical linear unit. Renderer-local coordinates, float reduction, and separate origin math never enter canonical bounds. Edges are closed (`min <= v <= max`); empty bounds are a defined state that contains and intersects nothing.

## Logical chunking

The initial logical partition uses square cells with a default 1 km edge in project space (`defaultChunkSize = 1000` in the canonical linear unit). The value is project/runtime configuration (`ChunkGridConfig`), not encoded into entity identity, and is defined once rather than hard-coded at use sites.

Cell identity (`ChunkCoord`, two signed 64-bit indices) comes from mathematical **floor division** of canonical coordinates: negative coordinates floor, never truncate toward zero, and a position exactly on a cell edge belongs to the higher cell. Cells are sparse and logical — chunk identity represents a spatial partition only, and never allocates a dense world matrix.

Entities retain canonical domain identity (`EntityId`, 128-bit UUID-compatible) independent of chunks. Long entities may intersect multiple chunks; chunk membership is an index/derived mapping, not ownership. An entity may temporarily have no spatial bounds and occupy no cells.

## Boundary semantics

- Chunk *k* covers `[k*size, (k+1)*size)` for point mapping.
- Bounds enumeration is conservative: a bounds ending exactly on a cell edge touches both adjacent cells, so invalidation never misses content that shares only an edge.

## Spatial index and chunk diffing

The canonical `SpatialIndex` maps stable entity ids to bounds and occupied cells. Every accepted mutation (`insert`/`update`/`remove`) returns the spatial consequences computed once:

- **previous chunks** — cells covered by the old bounds (none for insert);
- **updated chunks** — cells covered by the new bounds (none for remove);
- **dirty chunks** — the deterministic union of both.

`dirty chunks = chunks(old bounds) ∪ chunks(new bounds)` is the only chunk-diff rule: terrain, roads, simulation, and the renderer consume it through `ChunkDirtySet`, never by recomputing bounds math. A local edit produces a dirty set bounded by the entity's old/new coverage — never a full-world rebuild.

## Content classes and invalidation

Changes declare typed invalidation classes — `geometry`, `material`, `topology`, `terrain`, `simulation` (`InvalidationClass`/`InvalidationMask`, composable per chunk) — so affected chunks are computed from old/new bounds plus the domain classes the change dirties. A chunk may reference derived payload for terrain, road render geometry, infrastructure, environment instances, simulation spatial data, and renderer acceleration structures.

## Streaming

Renderer residency is determined from camera/working-set policy, not total project extent. Cells transition through explicit states — `unloaded → loading → resident → stale → evicting → unloaded` (loading may fail back to `unloaded`; stale content may be rebuilt via `loading` or dropped via `evicting`) — validated by `ChunkResidencyTracker`. Eviction and staleness do not modify canonical project entities, and the complete world is never assumed resident.

## Generated data

Chunk render/cache files carry versioned metadata — cache schema version, generator revision, and the canonical source revision they were derived from (`ChunkCacheMetadata`). Schema/tool/source drift marks the content stale and it is rebuilt instead of interpreted. Chunk caches are content-versioned, rebuildable, and never a storage location for canonical project data; no canonical entity exists exclusively because a chunk cache contains it.
