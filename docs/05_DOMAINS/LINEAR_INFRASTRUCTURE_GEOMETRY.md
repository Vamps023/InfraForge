# Linear infrastructure geometry — source linework to canonical alignment

## Purpose

Road and rail source data such as OpenStreetMap commonly arrives as piecewise-linear node sequences. Those nodes are valuable source evidence and topology, but they are not automatically suitable as final engineering geometry or final render geometry. InfraForge must preserve the source while producing smooth, topology-safe canonical alignments.

This document defines the shared road/rail geometry-conditioning contract used by authored content, OSM-derived imports, future GIS adapters, and rail workflows.

## Core rule: preserve source, derive alignment

InfraForge keeps distinct representations with explicit ownership:

1. **Source geometry** — immutable imported node/way geometry plus provenance and source IDs.
2. **Conditioned geometry** — cleaned/projected working polyline used during fitting; rebuildable and not authoritative on its own.
3. **Canonical alignment** — persisted mathematical infrastructure geometry composed of line, circular-arc, and clothoid/spiral segments where supported by the owning domain.
4. **Render tessellation** — dense GPU-facing vertices generated from the canonical alignment for the active LOD/view.

Raw OSM/GIS polyline vertices must not become the only canonical road or rail representation, and GPU line smoothing must not be mistaken for improved canonical data.

## Required pipeline

```text
source adapter
  -> retain source IDs/tags/coordinates/provenance
  -> canonical Geo transform into project metric coordinates
  -> topology analysis and protected-anchor extraction
  -> duplicate/degenerate/near-collinear cleanup
  -> optional display-preview smoothing
  -> line/arc/clothoid alignment fitting
  -> deviation/continuity/topology validation
  -> transactional canonical commit
  -> chunk-aware render tessellation/LOD
```

All geometric fitting and tolerances operate in the canonical project coordinate system using metric/world units. Do not smooth, fit radii, or evaluate deviation directly in latitude/longitude degrees.

## Protected anchors and topology

Smoothing or fitting must not move through topology-defining anchors. At minimum the fitting pipeline must protect:

- true road junction/connectivity nodes;
- rail switches/turnouts and branch points;
- alignment endpoints that are semantically meaningful;
- bridge/tunnel portals when source semantics require a fixed transition point;
- level crossings and other cross-domain attachment points when bound by canonical identity;
- user-pinned control points;
- importer conflict points that require manual resolution.

OSM way boundaries alone are not automatically physical anchors. Compatible ways split only for tagging/storage reasons may be merged into one fitting span when topology and semantics prove that doing so is safe.

Chunk/tile boundaries are never allowed to create canonical geometry breaks. Derived tessellation across chunk boundaries must remain continuous and deterministic.

## Display smoothing versus engineering alignment

### Display smoothing

For fast previews and source inspection, InfraForge may generate a non-canonical smoothed polyline. Chaikin corner cutting is an acceptable reference technique for this layer because it removes visually harsh corners by adding/interpolating vertices. Catmull-Rom or another deterministic interpolation may also be used when justified.

Display smoothing is always derived data. It must be possible to toggle back to the original source geometry, and preview smoothing must never silently change persisted road/rail topology or engineering parameters.

GeoLibre's vector `Smooth` tool is a useful behavioral reference for Chaikin-based line smoothing, but InfraForge must implement its own domain-appropriate pipeline and must not introduce a GeoLibre runtime dependency for this feature.

### Engineering alignment

Road and rail canonical geometry should prefer mathematical primitives rather than an arbitrary spline/polyline-only representation:

```text
Line -> Clothoid/Spiral -> Circular Arc -> Clothoid/Spiral -> Line
```

The fitter must be deterministic for the same source/configuration and must expose enough diagnostics to explain where a fit could not satisfy its constraints.

Do not invent a design speed, minimum radius, cant/superelevation, transition length, or similar engineering value when the source/configuration does not provide one. Missing design constraints must remain explicit and may require user input or a documented non-engineering display-only fallback.

## Deviation and fitting constraints

The fitting service must support policy/configuration for at least:

- maximum lateral deviation from retained source geometry;
- minimum source span/vertex count before a curve is attempted;
- continuity requirements between fitted segments;
- optional radius/transition constraints supplied by domain policy or user input;
- resampling/tessellation tolerance for derived rendering;
- deterministic handling of short/degenerate segments.

Failure to satisfy required constraints must produce a typed diagnostic/import report entry. The importer must not silently force a visually pleasing curve that exceeds the configured deviation or disconnects topology.

## OSM-derived roads

The OSM road adapter must:

- retain OSM way/node IDs and source tags as provenance;
- transform coordinates through the shared Geo service;
- build/resolve connectivity before smoothing/fitting;
- distinguish true junctions from artificial way splits;
- fit each topology-safe span through the shared alignment service;
- retain both original source geometry and the resulting canonical alignment;
- report ambiguous lane count, speed, direction, class, and fit constraints rather than silently fabricating them;
- allow the user to compare `Source` and `Canonical Alignment` geometry during import review/inspection.

## Rail

Rail reuses domain-neutral alignment mathematics where appropriate but owns rail-specific topology and semantics. In particular:

- turnouts/switches are protected topology entities, not points to smooth through;
- rail import/authoring may use line/arc/clothoid primitives from the shared alignment kernel;
- track gauge, cant, speed zones, signalling, and turnout geometry remain rail-domain semantics;
- imported OSM/GIS rail linework follows the same source -> conditioned -> fitted -> validated flow;
- arbitrary spline smoothing must not be presented as canonical railway engineering geometry.

## Rendering

The renderer consumes tessellated alignment samples, not raw source nodes as the final road/rail geometry. Tessellation density may vary by LOD/camera needs, but the sampled curve must converge on the same canonical alignment and remain seam-safe across chunks.

Rounded GPU joins/caps and antialiasing are visual quality improvements only. They complement, but do not replace, canonical curve reconstruction.

## Validation requirements

Road/rail validators should detect at least:

- topology changed or disconnected by fitting;
- source-to-alignment deviation above configured tolerance;
- positional discontinuity between adjacent alignment segments;
- tangent/curvature continuity failures where required;
- zero/near-zero-length segments;
- invalid/negative/non-finite radius or transition parameters;
- branch/junction/turnout movement without an explicit topology mutation;
- render tessellation seams that do not sample the same canonical boundary point.

Diagnostics reference canonical entity IDs and source provenance where available.

## Minimum verification fixtures

Automated tests must include:

1. a coarse polyline bend that becomes a visibly smooth derived curve while remaining within configured deviation;
2. a T/X road junction whose shared anchor position/connectivity is unchanged after fitting;
3. two OSM ways split for metadata only that can be joined and fitted as one continuous span when semantics permit;
4. a rail branch/turnout that is never smoothed through as if it were one continuous track;
5. repeated import/fitting of the same fixture producing deterministic segment parameters;
6. high-latitude/geographic source data proving fitting happens after projection into canonical project coordinates;
7. chunk-boundary tessellation proving no visible or positional seam;
8. source-vs-canonical inspection proving the original geometry remains recoverable.

## Related implementation issues

- #7 — road authoring and shared mathematical alignment services
- #8 — canonical road/lane junction topology
- #9 — OSM-derived road/OpenDRIVE import adapters and source-to-alignment conversion
- #13 — rail domain, rail alignment, turnouts, and rail import behavior
- #14 — unified structured validation/diagnostics
