# World partition and chunks

World partition limits processing and renderer residency for large projects. The domain foundation is implemented in `engine/src/domain/world/` (see `docs/05_DOMAINS/WORLD.md`); this document defines the data semantics.

## Canonical bounds

Spatial bounds are axis-aligned rectangles in **project-global space** — the canonical double-precision easting/northing frame of the Geo domain (ADR-0007), expressed in the canonical linear unit. Renderer-local coordinates, float reduction, and separate origin math never enter canonical bounds. Edges are closed (`min <= v <= max`); empty bounds are a defined state that contains and intersects nothing.

## Logical chunking

The initial logical partition uses square cells with a default **physical 1 km edge** (`defaultChunkEdgeMetres = 1000`, the single definition point of the physical default). Project-global coordinates are expressed in the project's canonical linear unit (ADR-0007), so the production grid is built through `ChunkGrid::fromMetreEdge(linearUnit, edgeMetres)`, which converts the metre edge through the resolved linear unit (`ResolvedUnit::toMetre`): a metre project and a US-survey-foot project get physically identical 1 km cells. There is no default project-unit edge — a grid in project units (`ChunkGridConfig`) always states its size explicitly. Chunk size is project/runtime configuration, not encoded into entity identity, and is never hard-coded at use sites.

Cell identity (`ChunkCoord`, two signed 64-bit indices) comes from mathematical **floor division** of canonical coordinates: negative coordinates floor, never truncate toward zero, and a position exactly on a cell edge belongs to the higher cell. Cells are sparse and logical — chunk identity represents a spatial partition only, and never allocates a dense world matrix.

Entities retain canonical domain identity (`EntityId`, 128-bit UUID-compatible) independent of chunks. Long entities may intersect multiple chunks; chunk membership is an index/derived mapping, not ownership. An entity may temporarily have no spatial bounds and occupy no cells.

## Boundary semantics

- Chunk *k* covers `[k*size, (k+1)*size)` for point mapping.
- Bounds enumeration is conservative: a bounds ending exactly on a cell edge touches both adjacent cells, so invalidation never misses content that shares only an edge.
- Two separate constraints bound the usable chunk range: (1) integer chunk indices have an absolute double-exactness ceiling at ±(2^53 − 1) (`maxExactChunkIndex`), beyond which indices cannot be converted to double coordinates exactly, and (2) actual usable cell extent depends on the configured chunk size — a cell exists only while its boundary products `k*size` and `(k+1)*size` remain finite, strictly increasing doubles. Once the products' floating-point spacing reaches the cell edge (for many valid sizes well below the ceiling), the cell collapses to zero width and mapping into or enumerating it fails loudly (`CoordinateOutOfRange`) instead of producing a degenerate footprint.

## Spatial index and chunk diffing

The canonical `SpatialIndex` maps stable entity ids to bounds and occupied cells. Every accepted mutation (`insert`/`update`/`remove`) returns the spatial consequences computed once:

- **previous chunks** — cells covered by the old bounds (none for insert);
- **updated chunks** — cells covered by the new bounds (none for remove);
- **dirty chunks** — the deterministic union of both.

`dirty chunks = chunks(old bounds) ∪ chunks(new bounds)` is the only chunk-diff rule: terrain, roads, simulation, and the renderer consume it through `ChunkDirtySet`, never by recomputing bounds math. A local edit produces a dirty set bounded by the entity's old/new coverage — never a full-world rebuild.

## Content classes and invalidation

Every index mutation states its typed invalidation classes explicitly — `geometry`, `material`, `topology`, `terrain`, `simulation` (`InvalidationClass`/`InvalidationMask`, composable per chunk) — so affected chunks are computed from old/new bounds plus the domain classes the change dirties. An empty mask is a deliberate declaration ("no generated work"): the mutation still reports its spatial chunk diff, but it creates no dirty entries and advances no content generations. A chunk may reference derived payload for terrain, road render geometry, infrastructure, environment instances, simulation spatial data, and renderer acceleration structures.

## Streaming

Renderer residency is determined from camera/working-set policy, not total project extent. Cells transition through explicit states — `unloaded → loading → resident → stale → evicting → unloaded` (loading may fail back to `unloaded`; stale content may be rebuilt via `loading` or dropped via `evicting`) — validated by `ChunkResidencyTracker`. Eviction and staleness do not modify canonical project entities, and the complete world is never assumed resident.

## Generated data

Chunk render/cache files carry versioned metadata — cache schema version, generator revision, and the canonical source revision they were derived from (`ChunkCacheMetadata`). The source revision for chunk-scoped content is the **dependency-scoped, per-chunk content generation** (`SpatialIndex::lastAffectingRevision(chunk, dependencyMask)`): generations are tracked per invalidation class per cell, and a mutation advances only the classes it declared. A distant one-entity edit therefore leaves unrelated chunk caches current and can never drag the whole world back to stale, and within one chunk a Material-only edit never stales a Terrain-dependent cache. Schema/tool drift marks the content stale and it is rebuilt instead of interpreted. Chunk caches are content-versioned, rebuildable, and never a storage location for canonical project data; no canonical entity exists exclusively because a chunk cache contains it.
