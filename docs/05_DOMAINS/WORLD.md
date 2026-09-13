# Domain: World (large-world partition foundation)

**Status:** Implemented (in-memory domain foundation, issue #4); verified by
doctest suites. Consumer integration (renderer streaming, terrain, roads,
simulation) lands with their issues; there is deliberately no protocol or
persistence surface yet.

The World domain owns the canonical large-world vocabulary defined by
ADR-0008 and `docs/02_DATA/WORLD_CHUNKS.md`: canonical spatial bounds, the
logical chunk grid, the spatial index with its incremental chunk-diff
mutations, typed invalidation classes, chunk residency states, and
generated-cache metadata. None of these concepts are owned by the renderer;
the renderer consumes them.

## Coordinate model

Bounds and chunking operate in **project-global space** — the canonical
double-precision easting/northing frame established by the Geo domain
(ADR-0007). No renderer-local coordinates, CRS conversions, origin math, or
scale factors exist here. `SpatialBounds` is horizontal (easting/northing)
because the logical partition is a horizontal grid; vertical extent is not
part of chunk identity.

## Native modules

| Module | Role |
|---|---|
| `domain::world::EntityId` (`EntityId.hpp`) | Stable 128-bit UUID-compatible entity identifier. Pure identity token — chunk membership is derived index state, never part of identity; the reserved all-zero value is rejected by the index. |
| `domain::world::SpatialBounds` (`SpatialBounds.hpp`) | Canonical axis-aligned double-precision bounds in project-global space. Closed edges, explicit empty state, union/intersection/containment/expansion. |
| `domain::world::ChunkGrid` (`ChunkGrid.hpp`/`.cpp`) | Logical chunk cells (`ChunkCoord`), configurable `ChunkGridConfig` (explicit project-unit edge; the physical default `defaultChunkEdgeMetres` = 1 km is converted through the resolved canonical linear unit via `ChunkGrid::fromMetreEdge`, so non-metre projects get physically identical cells), floor-division mapping, conservative closed-bounds enumeration, cell footprints. |
| `domain::world::Invalidation` (`Invalidation.hpp`/`.cpp`) | `InvalidationClass` (geometry, material, topology, terrain, simulation) and the composable `InvalidationMask` bit set. Strongly typed — never free-form strings; every mutation states its classes explicitly. |
| `domain::world::SpatialIndex` (`SpatialIndex.hpp`/`.cpp`) | Canonical sparse index: entity id → canonical bounds → occupied cells. `insert`/`update`/`remove` return an `IndexMutation` carrying previous cells, updated cells, their deterministic union (dirty), the declared invalidation classes, and the new index revision. Per-cell content generations (`lastAffectingRevision`) advance only for the cells a mutation actually dirtied, so generated caches of unrelated chunks stay current across local edits. `ChunkDirtySet` accumulates per-cell class masks across mutations — the one canonical chunk-diff mechanism. |
| `domain::world::ChunkResidency` (`ChunkResidency.hpp`/`.cpp`) | `ChunkResidencyState` (unloaded, loading, resident, stale, evicting) and `ChunkResidencyTracker` validating transitions. Transient derived state, never canonical project truth. |
| `domain::world::ChunkCacheMetadata` (`ChunkCacheMetadata.hpp`) | Versioned provenance record (schema version, generator revision, per-chunk source generation) with `isCurrent` staleness checking. Caches are always reconstructable from canonical data. |
| `domain::world::WorldPartitionError` (`WorldPartitionError.hpp`) | Failure taxonomy: invalid chunk size, invalid/non-representable bounds or coordinates, null/duplicate/unknown entity, illegal residency transition. Violations fail loudly — never clamped or absorbed. |

## Boundary semantics

- Chunk cell *k* covers the half-open footprint `[k*size, (k+1)*size)` on
  an axis; mapping uses mathematical floor division, so negative
  coordinates floor (never truncate toward zero) and a position exactly on
  an edge belongs to the higher cell.
- Bounds edges are closed: `contains` is `min <= v <= max`, rectangles
  touching at an edge intersect, and cell enumeration is conservative — a
  bounds ending exactly on a cell edge dirties both adjacent cells. Empty
  bounds select no cells and an entity may temporarily have no spatial
  extent.
- Chunk indices are supported in the symmetric range ±(2^53 − 1): at
  exactly 2^53 the footprint's `(k+1)` upper edge would round back onto
  `k` and collapse the cell. Mapping is rejected
  (`CoordinateOutOfRange`) beyond that range, for non-finite input, and
  when a bounds spans more than `ChunkGrid::maxEnumeratedChunks` cells.

## Invalidation model

`dirty chunks = chunks intersecting the old bounds ∪ chunks intersecting
the new bounds`, computed once by the index. Moving or shrinking an entity
invalidates the cells it left as well as the cells it entered. A local edit
produces a dirty set bounded by the entity's old/new coverage — never a
full-world rebuild. Each mutation declares its invalidation classes
explicitly; an empty `InvalidationMask` is an intentional "no generated
work" declaration, not a default.

## Renderer residency and cache separation

Residency (`unloaded → loading → resident → stale → evicting → unloaded`,
with failure/cancellation edges) describes derived content only. Eviction
and staleness never modify canonical entities, and the full project is
never assumed resident. Generated-cache metadata records schema version,
generator revision, and the chunk's per-cell content generation
(`SpatialIndex::lastAffectingRevision`, which advances only for the cells a
mutation dirtied) — so a distant local edit leaves unrelated chunk caches
current and can never invalidate the whole world. Any schema, generator,
or generation drift marks the cache stale for rebuild. Caches are never
canonical storage — no entity exists only because a chunk cache contains
it.

## Large-world behavior

Storage and queries scale with occupied/query cells and result size, never
with total world extent; empty regions cost nothing (see
`occupiedChunkCount`, `ChunkResidencyTracker::trackedChunkCount`). The
acceptance test (`engine/tests/LargeWorldIndexTests.cpp`) builds a
synthetic 100 km × 100 km project with 2,000 entities, cross-checks the
entire index against independent ground truth, verifies brute-force-equal
queries across ±49 km in all quadrants including negative coordinates, and
proves a one-entity edit dirties at most 4 of the 10,000 dense-grid cells
while only resident dirty cells go stale.

## Threading

All mutable world-domain state is single-threaded, owned by the
application executor thread like canonical project state; it is not
internally synchronized. `ChunkGrid`, `SpatialBounds`, and metadata types
are immutable values.

## Verification

- `engine/tests/SpatialBoundsTests.cpp` — empty/point/closed-edge
  semantics, union/intersection/containment, negative coordinates,
  finiteness.
- `engine/tests/ChunkGridTests.cpp` — physical 1 km default converted
  through metre and US-survey-foot linear units (identical physical cells)
  and broken-unit rejection, explicit project-unit sizes, floor division
  and exact-boundary behavior, single/multi-cell and negative enumeration,
  footprint round-trip, the ±(2^53 − 1) representable range with full-width
  edge cells, invalid size and unrepresentable-coordinate rejection,
  enumeration cap.
- `engine/tests/SpatialIndexTests.cpp` — insert/move/expand/shrink/remove
  mutations, old∪new dirty union, shared and multi-cell entities,
  bounds-checked queries, dormant entities, all five invalidation classes
  and masks, dirty-set accumulation, error paths, chunk-vs-entity identity
  independence, per-chunk content generations keeping unrelated chunks
  current across local edits (the anti-full-world-invalidation regression).
- `engine/tests/ChunkResidencyTests.cpp` — full lifecycle, failure and
  drop paths, illegal-transition rejection, sparse tracking.
- `engine/tests/ChunkCacheMetadataTests.cpp` — schema versioning and
  staleness on any drift.
- `engine/tests/LargeWorldIndexTests.cpp` — the 100 km acceptance case,
  including per-chunk cache currency for distant cells during a local
  edit.

## Limitations

- The partition is 2D horizontal; vertical chunking can be layered on
  when a domain needs it.
- The index is in-memory; persistence of entity data arrives with the
  entity domains (terrain/roads), which will own canonical storage.
- No protocol/persistence surface by design — nothing crosses the process
  boundary until a real externally observable operation exists.
