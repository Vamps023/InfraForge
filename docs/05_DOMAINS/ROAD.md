# Road Domain — Canonical Geometry Foundation

## Overview

The road domain owns canonical road geometry as infrastructure/domain truth.
Road geometry is **not** renderer mesh data and **not** frontend state. The
architecture is:

```
Canonical Road (domain)
  → ReferenceAlignment (Line / CircularArc / Clothoid)
  → ElevationProfile
  → SuperelevationProfile
  → Derived Tessellation (future, rebuildable)
  → Render Projection (future, renderer-owned)
```

Never: `Rendered Mesh → Road Truth`.

This document covers the foundation established on `feature/road-foundation`
(Issue #7). The full Road authoring UI, lane/junction topology, OSM/OpenDRIVE
import, traffic, and simulation are explicitly out of scope for this foundation
and belong to subsequent stages.

## Canonical Ownership

- **Road truth lives in the C++ domain layer** (`engine/include/infraforge/domain/road/`).
- The renderer consumes derived tessellation and must not own canonical roads.
- GPU meshes, generated tessellation, and caches are disposable/rebuildable.
- React owns presentation and transient UI state only; it does not own road geometry.
- Roads operate entirely in canonical project coordinates (double precision).
- Road truth is independent of terrain: roads are valid in an empty project.

## Coordinate System

All road engineering geometry operates in canonical project-global coordinates
(`ProjectGlobalPosition`: easting, northing, height) using the project's
canonical linear unit. Road geometry is **never** evaluated in WGS84
latitude/longitude degrees. The centralized `ProjectGeoreference` resolves CRS
and canonical linear units; the road domain reuses it and does not introduce a
parallel CRS/origin model.

## Alignment Primitives

The reference alignment is composed exclusively of three mathematical
primitives. Polylines and tessellated vertices are never canonical alignment
truth.

### Line (`LineSegment`)

Mathematically exact straight segment. Canonical parameters: start point,
constant heading, length. Curvature is identically zero.

```
position(s) = start + s * (cos(heading), sin(heading))
heading(s)  = heading
curvature(s) = 0
```

### Circular Arc (`CircularArcSegment`)

Deterministic signed-curvature arc. Canonical parameters: start point, start
heading, signed curvature (!= 0), length. Positive curvature turns left (CCW),
negative turns right (CW). Never approximated by a polyline.

```
theta(s)    = startHeading + curvature * s
easting(s)  = start.easting  + (1/kappa) * (sin(theta(s)) - sin(startHeading))
northing(s) = start.northing + (1/kappa) * (cos(startHeading) - cos(theta(s)))
heading(s)  = theta(s)
curvature(s) = kappa
```

### Clothoid / Spiral (`ClothoidSegment`)

Euler spiral transition where curvature changes linearly with station.
Canonical parameters: start point, start heading, start curvature, end
curvature, length. Supports: 0→positive, positive→0, 0→negative, negative→0,
and general curvature A→B.

```
kappa(s) = startCurvature + (endCurvature - startCurvature) * s / length
theta(s) = startHeading + startCurvature * s + alpha * s^2
position(s) = start + ∫₀ˢ (cos theta, sin theta) dt
```

Heading and curvature are closed form; position is a Fresnel-type integral
evaluated by a deterministic, allocation-free composite 8-point Gauss-Legendre
quadrature. Tessellated vertices are never stored as canonical clothoid truth.

## Continuous Stationing

`ReferenceAlignment` owns continuous stationing. Station 0 is the alignment
start; segment i ends at the station segment i+1 begins. There are no hidden
gaps or overlaps. Evaluation at boundaries is deterministic:

- Stations outside `[0, totalLength]` clamp to the nearest end (no extrapolation).
- Segment lookup at an internal boundary returns the earlier segment.

## Continuity Validation

The alignment builder validates:

- **G0**: positional continuity between adjacent segments
- **G1**: heading/tangent continuity (modulo 2π)
- **Curvature continuity**: end curvature of segment i = start curvature of segment i+1
- **Structural validation**: finite parameters, positive length, non-zero arc curvature
- **Station continuity**: no gaps or overlaps

Invalid engineering geometry is never silently repaired. The builder returns
typed diagnostics (`RoadDiagnostic`) describing all failures.

## Elevation and Superelevation Profiles

Both use a piecewise-linear representation over sorted (station, value)
breakpoints, compatible with future OpenDRIVE `<elevation>`/
`<superelevation>` records.

- Between breakpoints: linear interpolation
- Before first / after last: boundary value held constant
- Empty profile: evaluates to zero (no fabricated values)
- Independent of terrain

## Source Geometry and Provenance

Source geometry and provenance are architecturally separate from the canonical
alignment:

```
SourcePolyline (source coordinates, e.g. WGS84)
  → Geo transform into project coordinates
  → Protected-anchor extraction
  → Line/Arc/Clothoid fit
  → Validation
  → Canonical ReferenceAlignment
```

- Source geometry is **never** the canonical road geometry (OSM nodes are not
  the alignment).
- `RoadSource` holds the original linework, provenance (provider, source ID,
  tags, import timestamp), and protected anchors.
- Authored roads have an empty `SourcePolyline` and `provider == Authored`.
- Protected anchors (junction locations, endpoints, user-pinned points) are
  reference data that future fitting/smoothing must not move.
- The `AlignmentFitInput`/`AlignmentFitResult` interface establishes the
  contract for the future shared fitter. The full OSM/OpenDRIVE importer is
  out of scope (issues #9/#15).

## Persistence

Road persistence integrates with the existing SQLite/project architecture:

- **Migration 7** ("road canonical geometry") creates explicit tables for
  roads, segments, elevation/superelevation breakpoints, source data, source
  vertices, and protected anchors.
- `ProjectStore` port gains `roads()`, `insertRoad()`, `removeRoad()`.
- `SqliteProjectStore` implements these with transactional commits that advance
  the project revision.
- `RoadRecord` is a flat projection for serialization; `toRecord`/`fromRecord`
  convert between the canonical `Road` and the serializable record.
- Save/reopen reconstructs the exact same road parameters: stable IDs survive,
  segment types/parameters/curvatures are preserved, profiles and source
  geometry/provenance/anchors are preserved.

## Determinism

Given identical canonical road parameters, evaluation is identical. The
clothoid quadrature uses fixed constants and bounded deterministic subdivision.
Tests use explicit tolerances.

## Future Integration

- **Lanes/junctions** (Issue #8): will attach to the reference alignment via
  cross-sections and topology nodes.
- **OSM/OpenDRIVE import** (Issue #9/#15): will use the fitter interface to
  convert conditioned source polylines into canonical alignments.
- **World partition**: roads will register with `WorldState` for spatial
  indexing and chunk invalidation.
- **Renderer**: will consume derived tessellation, not canonical geometry.
- **Terrain integration**: terrain may provide elevation snapping, but the
  road domain functions independently.
