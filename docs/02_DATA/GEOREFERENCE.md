# Canonical georeference

A project has one canonical georeference owned by the Geo domain
(`infraforge::domain::geo`). It is persisted in `project.db` (schema
migration 2) and mirrored in `project.json` for discovery. Implemented in
`engine/src/domain/geo/`; the PROJ library (vcpkg `proj`, versioned in
`vcpkg.json`) resolves CRS definitions and executes transforms.

## Stored fields (`GeoreferenceConfig`)

- `horizontalCrs` — CRS identifier or definition text (EPSG code, OGC URN,
  WKT2, PROJJSON, PROJ string); resolved by PROJ at open/create.
  Canonicalization substitutes `AUTH:CODE` only when the definition is
  canonically equivalent to the authority entry — bound CRSs and custom
  definitions keep their original form so encoded transformation
  semantics are never lost.
- `linearUnit` — linear unit of measure for domain world coordinates,
  resolved against the PROJ unit database (e.g. `metre`, `US survey foot`).
- `axisConvention` — `EASTING_NORTHING_UP` (the only supported convention;
  other axis orders are rejected).
- `originEasting` / `originNorthing` / `originHeight` — project origin
  expressed in the horizontal CRS frame and the vertical reference, all in
  the canonical linear unit.
- `verticalCrs` — optional vertical CRS/datum identifier; must resolve to
  a vertical CRS in PROJ when present (rejected at create/set/open
  otherwise).

Traffic side is project traffic semantics and is stored separately from
coordinate transforms.

## Origin semantics

- The project origin anchors the render-local frame; it is stored in the
  horizontal CRS's own axis order (easting/northing of the CRS definition,
  not lon/lat).
- `originHeight` is expressed in the canonical linear unit relative to the
  vertical reference. It was added by schema migration 2; schema v1
  databases default it to 0.
- `axisConvention` controls world-component ordering only. Source
  coordinates always use normalized GIS-friendly order
  (`proj_normalize_for_visualization`): longitude/latitude for geographic
  sources, easting/northing for projected sources — never EPSG-native
  latitude/longitude order.

## Coordinate spaces

1. **Source coordinate space** — coordinate system used by imported source
   data. The source horizontal CRS must resolve to a geographic (2D or
   3D), projected, or engineering CRS; geocentric, compound, vertical, and
   other role forms are rejected explicitly. Coordinates are always in
   normalized axis order (x = longitude or easting, y = latitude or
   northing); a source that supplies EPSG:4326 sends x=longitude,
   y=latitude.
2. **Project global space** — double-precision canonical domain coordinates
   in project axis order and linear unit. The Geo service normalizes source
   coordinates into project global space, then scales CRS axis units into
   project linear units.
3. **Render local space** — coordinates relative to a render origin.
   `RenderLocalFrame` (header-only, shared with the renderer) performs the
   double-precision origin-relative conversion (positions are already in
   the canonical linear unit); the renderer may cast the result to `float`
   at the GPU boundary.

All source transformations go through the shared Geo service
(`GeoTransformService`). Terrain, roads, OSM, buildings, simulation, and
exporters may not maintain independent origin/scale/CRS logic.

## Resolved metadata

`resolveProjectGeoreference` returns `ProjectGeoreference`: the canonical
config plus resolved metadata (normalized identifier, display name, CRS
kind, per-axis unit-to-metre factors, linear-unit factor, vertical
reference info). A configured vertical CRS must resolve to a real vertical
CRS — unresolvable or wrong-role definitions fail resolution outright
(there is no metadata-only acceptance). The resolved object is recomputed
on demand — it is never persisted or cached as canonical state.

## Transformation strictness

`GeoTransformService` creates coordinate operations through
`proj_create_crs_to_crs_from_pj` with `ALLOW_BALLPARK=NO` and
`ONLY_BEST=YES`:

- ballpark/low-accuracy fallback operations are never selected;
- when the best known transformation cannot be instantiated (for example a
  required grid file is not part of the deployment), operation creation or
  execution fails and the error surfaces through the explicit
  `GeoError::UnsupportedTransform` → `COMMAND_ERROR_CODE_GEO_UNSUPPORTED`
  path — never as a silently degraded coordinate.

## Height semantics

- Source `z` is interpreted in the source vertical CRS's axis unit. When no
  source vertical CRS is supplied, heights are assumed to be metres
  (PROJ's convention for 2D/geographic source data).
- Project-global height is expressed in the project's canonical linear
  unit; source heights are converted even when no vertical datum transform
  applies.
- A compound datum transformation (source and project vertical CRSs both
  present and different) is delegated to PROJ and applies grid/height
  models where available — subject to the strictness rules above.
  Compound CRSs are composed through `proj_create_compound_crs` from the
  resolved horizontal and vertical CRS objects, so every accepted
  definition syntax (authority identifier, OGC URN, WKT2, PROJJSON) can
  take part in a vertical transformation.
- Whether the source and project vertical references differ is decided on
  resolved PROJ objects (`proj_is_equivalent_to_with_ctx`), not raw
  strings — an EPSG identifier and its OGC URN form are the same
  reference and trigger no datum transformation.

## Supported and unsupported behavior

- Invalid CRS definitions throw `GeoError::InvalidCrs` (surfaced as
  `COMMAND_ERROR_CODE_INVALID_ARGUMENT`).
- Well-formed but unknown/unresolvable identifiers and transformations
  whose required support is unavailable throw `GeoError::Unsupported*`
  (surfaced as `COMMAND_ERROR_CODE_GEO_UNSUPPORTED`).
- A missing PROJ database is a startup-level failure: the engine's
  `--self-check` and `serve` paths fail loudly instead of silently
  degrading. `PROJ_DATA`/`PROJ_LIB` are honored when set; otherwise the
  compiled-in vcpkg share path and executable-relative locations are probed.
- A vertical CRS that PROJ cannot resolve makes the persisted project
  unopenable through the service boundary: `project.open` revalidates the
  canonical georeference and leaves no active session on failure.

## Precision

Canonical horizontal/vertical computations use double precision. Renderer
conversion to `float` occurs only at the render boundary with the
documented origin transform (`RenderLocalFrame`).

## Change policy

Changing a project's CRS/origin is a project-level mutation through
`geo.set_georeference`: it revalidates via PROJ, rewrites `project.db` and
`project.json` in one store operation, increments `project_state.revision`,
and emits `georeference_changed` + revision/dirty events. It is not a
simple UI preference.
