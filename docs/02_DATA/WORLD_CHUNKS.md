# World partition and chunks

World partition limits processing and renderer residency for large projects.

## Logical chunking

The initial logical partition uses square cells with a default 1 km edge in project space. The value is project/runtime configuration, not encoded into entity identity.

Entities retain canonical domain identity independent of chunks. Long entities may intersect multiple chunks; chunk membership is an index/derived mapping, not ownership.

## Chunk content classes

A chunk may reference derived payload for terrain, road render geometry, infrastructure, environment instances, simulation spatial data, and renderer acceleration structures.

## Dirty tracking

Changes emit specific invalidations such as:

- geometry dirty;
- material dirty;
- terrain dirty;
- topology dirty;
- simulation graph dirty;
- asset instance dirty.

Affected chunks are computed from old/new bounds plus domain dependencies.

## Streaming

Renderer residency is determined from camera/working-set policy, not total project extent. Chunks transition through explicit loading/resident/unloading/error states. Eviction does not modify canonical project entities.

## Generated data

Chunk render/cache files are content-versioned and rebuildable. Schema/tool version changes invalidate incompatible generated data instead of attempting unsafe interpretation.