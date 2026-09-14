# Domain: Terrain

**Status:** Issue #6 in progress. Both the local GeoTIFF import/sampling/tiling/renderer path and the `Download Area` draw/select/download-selected path are implemented on the issue branch. The Download Area path uses a real production terrain DEM provider (AWS Terrain Tiles / Terrarium) with proper HTTP fetch, GDAL-based PNG decoding, and canonical GeoTIFF assembly. A mock provider is retained for deterministic unit tests only. Issue #6 remains open pending manual runtime verification and final review of PR #28.

The Terrain domain owns canonical elevation datasets imported from real georeferenced rasters or acquired from supported remote DEM providers. It is the first authoring-scale domain built on the World partition (issue #4) and the Geo service's single canonical georeference (ADR-0007).

## Acquisition modes

Terrain import has two production source modes. They must converge into the same canonical validation, persistence, sampling, derived-tile generation, scene projection, and Vulkan renderer path as early as practical.

### Local File

`Terrain import -> Local File -> GeoTIFF/DEM -> native probe/import job -> canonical terrain dataset -> project-owned raster -> derived world tiles -> Vulkan`

The local-file path is the existing native GDAL import path. The original source file is never the long-term project dependency: successful import produces project-owned source storage.

### Download Area

`Terrain import -> Download Area -> map -> draw rectangular area -> application selection grid -> choose selected tiles -> native download plan -> native acquisition job -> decode/clip/assemble -> canonical terrain ingestion -> project-owned source storage/provenance -> derived world tiles -> Vulkan`

The selection is authoritative. If the user selects 3 of 20 application tiles, only provider data required for those 3 selected tiles may be acquired when the provider supports selective requests. Unselected gaps must not silently become terrain coverage.

The frontend owns only map/draw/selection UX and projection. The native engine owns provider planning, network acquisition, CRS transforms, decoding, NoData handling, clipping/resampling, canonical source construction, integrity/provenance, persistence, sampling truth, and derived render tiles.

## Canonical source of truth

- **Canonical record** — `terrain_datasets` rows in `project.db`: stable 128-bit dataset id (UUID text primary key, also the world-index `EntityId`), display name, source/provenance metadata, source CRS/geometry, elevation unit + metre factor, NoData semantics, canonical coverage, elevation range, integrity metadata, per-dataset revision, diagnostics, timestamps.
- **Project-owned source storage** — terrain source data lives inside the `.iforge` project. Local-file import copies the accepted source into project storage. Remote acquisition writes project-owned canonical source data/provenance so reopening never requires re-contacting the provider.
- **Derived, rebuildable** — `cache/terrain/<uuid>/tile_x_y.iforgetile` files are never canonical; deleting `cache/` costs rebuild time only.
- **No renderer truth** — generated meshes/tiles are derived presentation data and are never the elevation source of truth.

## Local-file import pipeline (cancellable job)

`terrain.import_dataset` runs as a `terrain.import` job on the engine worker:

1. **Probe** (executor): GDAL opens the source read-only. Validation is explicit — readable raster, supported numeric sample type, positive dimensions, supported north-up/non-rotated orientation, CRS present and resolvable (missing CRS is a typed failure, never an assumption), known elevation unit.
2. **Copy + hash** (worker): stream the source into project-owned temporary storage in bounded chunks; progress uses real work units; SHA-256 is computed while copying.
3. **Validate stored copy** (worker): the project-owned stored copy is authoritative for canonical raster metadata. The asynchronous import must not persist stale metadata from an earlier source probe if the external source changes between probe and copy.
4. **Coverage + range** (worker, worker-confined `GeoTransformService`): source coverage transforms to canonical project-global space in double precision; non-finite/unsupported results fail. Elevation range scans bounded strips; NoData cells are preserved and diagnosed.
5. **Canonical commit** (executor): publish source storage and persist the terrain dataset coherently. The database row and project-owned source must never disagree. Pre-commit failure leaves neither canonical row nor canonical raster. Post-commit failures in events or derived tile scheduling must not delete source storage behind an already committed row.
6. **Tiles** (worker): a follow-up `terrain.tiles` job generates derived renderer tiles for the canonical terrain coverage. Tile generation is derived cache work and cannot invalidate an otherwise valid committed dataset.

The import JobSystem lifecycle must stay `Running` through executor-side canonical finalization and become `Completed` only after canonical commit succeeds. Commit failure becomes authoritative `Failed`, never a false `Completed`.

Cancellation is cooperative at bounded checkpoints. A cancelled or pre-commit failed import leaves no canonical dataset row, no partial published source, no fake success event, and no orphan `.importing` file. Project close cancels tracked work and late results must be prevented from mutating a different/closed project.

## Draw-area selective acquisition

### Map and selection UX

The Terrain import surface provides `Local File` and `Download Area` modes. `Download Area` uses a real interactive Leaflet map (OpenStreetMap tiles with attribution) and supports:

- geographic map pan/zoom with OpenStreetMap basemap;
- click-and-drag rectangular drawing of a working area;
- go-to latitude/longitude input;
- visible selection grid overlay with selected/unselected styling;
- deterministic application selection grid over the area;
- 1 km, 2 km, 4 km, 8 km, and 16 km application selection tiles;
- click-to-toggle individual tiles directly on the map;
- Select All and Clear;
- selected tile count / total tile count;
- approximate selected area;
- provider/source and effective resolution;
- estimated download size when deterministically knowable;
- native download-plan warnings before execution.

A rectangle defines the working/selection area. It does **not** authorize silently downloading every point of the bounding box after the user deselects cells.

### Selection tiles vs provider tiles

Do not conflate these concepts:

- **TerrainSelectionTile** — user-facing project/application selection unit, expressed as a physical area (for example 4 km × 4 km).
- **Provider request tile/area** — provider-specific request unit (for example XYZ/WebMercator z/x/y tiles or a provider bounding-box request).

One selected application tile may require several provider requests. Adjacent selected application tiles may share provider requests. The planner must deduplicate shared upstream requests.

### Disconnected selections

Selections may contain gaps and disconnected islands. InfraForge must not automatically fill those gaps with fabricated terrain.

For an unselected gap:

- acquisition must not treat it as selected coverage;
- canonical terrain coverage must preserve the gap;
- sampling reports no terrain / outside coverage there;
- derived terrain tiles are not generated for that gap;
- the Vulkan scene must not render terrain there.

The persistence representation may be one logical dataset with an explicit coverage mask/pieces or another coherent canonical representation, but it must represent the true selected coverage rather than a filled bounding rectangle.

### Provider abstraction

Remote acquisition uses a native/backend provider abstraction (for example a `TerrainDownloadProvider` port with provider-specific adapters). Provider-specific URLs, decoding rules, authentication, coverage/resolution logic, and retry classification do not belong in React components.

A provider adapter owns at minimum:

- stable provider id/name;
- geographic coverage;
- native request CRS/scheme;
- resolution/zoom rules;
- deterministic request construction;
- authentication requirements;
- response decoding/elevation semantics;
- attribution/provenance metadata;
- retryable vs permanent failure classification.

At least one production-quality provider is required for Issue #6 completion. Its current endpoint, elevation encoding, legal terms, and attribution requirements must be verified at implementation time; do not blindly copy old OpenGeoStudio URLs.

Credentials are runtime secrets: never hard-code them, persist them into `.iforge`, or include them in logs/errors.

### Native download planning

Before execution, the engine produces a deterministic plan from provider + selected application tiles + project georeference. The plan contains enough information for the UI and tests to verify the work before download:

- normalized selected application tiles / coverage pieces;
- unique provider requests after deduplication;
- provider coverage compatibility;
- request/tile count;
- effective resolution;
- estimated bytes when knowable;
- typed warnings (partial provider coverage, unavoidable provider overfetch, etc.).

Suggested protocol separation is planning vs execution (for example `terrain.list_sources`, `terrain.plan_download`, `terrain.download_selected`), but final names must follow the protocol's established conventions.

### Selective acquisition rules

For each selected application tile:

1. Transform its coverage to the provider request scheme through the canonical Geo service.
2. Resolve the minimal intersecting provider requests needed for that selected coverage.
3. Deduplicate provider requests shared by adjacent selected tiles.
4. Download each unique upstream request once.
5. Decode elevation using provider-defined semantics without unnecessary precision loss.
6. Clip/resample to the selected canonical coverage while preserving NoData.
7. Build project-owned canonical terrain source data/provenance.
8. Enter the same canonical terrain validation/commit/tile-generation pipeline used by local-file import.

If a provider only exposes bounding-area requests, group conservatively and surface unavoidable overfetch in the plan. Do not silently turn a sparse selection into one huge bounding-box download.

### Remote job progress, retry, and cancellation

Remote acquisition is a real long-running JobSystem operation. Progress is based on real work and may include phases such as planning, provider requests/bytes, decoding, canonical assembly, validation, commit, and render-tile generation. Normalized progress must never move backward.

Cancellation checkpoints are required between provider requests and throughout bounded decode/assembly work. Cancelled/failed acquisition must leave no canonical row, no partial canonical source, no fake completion, and no orphan importing files.

Retry is bounded. Typed remote failures distinguish at minimum authentication failure, rate limiting, timeout/network failure, source unavailable, unsupported coverage, invalid/corrupt provider response, and cancellation.

Unit/CI tests use deterministic mock/local provider responses and must not require public internet access.

### Production terrain DEM provider

The production terrain DEM provider is **AWS Terrain Tiles (Terrarium)**:

- **Provider name:** `terrarium-aws`
- **Endpoint:** `https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png`
- **Elevation encoding:** Terrarium PNG — `height = (R * 256 + G + B/256) - 32768` (meters)
- **CRS:** Web Mercator (EPSG:3857)
- **Tile size:** 256×256 pixels
- **Coverage:** Global
- **Resolution:** Effective plan resolution is supplied by the provider via `effectiveResolutionMpp(const std::vector<SelectionTile>& selectedTiles)`, not computed by `TerrainService`. When no tiles are selected, it reports 0.0 m/px. When tiles are selected, it reflects the coarsest resolution across the selected coverage (evaluated at the minimum absolute latitude among selected tiles: `groundResolution = (tileSizeMeters * cos(minAbsLat)) / 256`). The provider's `maxResolutionMpp` is 0 (unknown) because the Terrarium dataset is a composite of multiple sources with varying native resolutions. `TerrainService` is provider-neutral and contains no Terrarium-specific zoom/resolution logic.
- **Authentication:** None required (AWS Open Data, S3 public bucket)
- **Attribution:** The Terrarium provider retains the full required attribution set derived from the authoritative joerd attribution documentation. Since Terrarium responses do not expose which underlying source contributed to each pixel/tile, a conservative full source set is retained persistently with the `TerrainDataset`. The attribution survives offline reopen and does not require re-contacting the provider. The Download Area UI shows a concise attribution summary with an expandable "View attribution" section exposing the full text. The Terrain Inspector provides access to the full persisted attribution similarly. Attribution wording is derived directly from the authoritative joerd attribution documentation at https://github.com/tilezen/joerd/blob/master/docs/attribution.md. Verified against joerd attribution documentation on 2026-09-14.
- **License:** Various open data licenses (USGS public domain, NASA, etc.)
- **Rate limits:** No published limits; bounded retry with exponential backoff handles transient failures
- **Download size:** Unknown (0) — PNG compression varies per tile, so size cannot be deterministically known before fetch. The protocol defines 0 = unknown; the UI displays `—`.
- **XYZ validation:** z in [0, 20], x in [0, 2^z), y in [0, 2^z) with overflow-safe arithmetic.
- **Tile boundary:** Half-open interval semantics for XYZ tile ranges:
  - **West/minX:** inclusive (first tile intersecting the area).
  - **East/maxX:** exclusive (an exact east boundary does NOT include the next tile to the east; the boundary is nudged toward `-inf` via `std::nextafter`).
  - **North/maxY:** inclusive (first tile intersecting the area).
  - **South/minY:** exclusive (an exact south boundary does NOT include the next tile to the south; the boundary is nudged toward `+inf` via `std::nextafter`).
  - World edges are clamped to `[0, 2^z)` with no magic epsilon.
  - This ensures no required provider tile is missed and no extra tile is fetched on exact east or south boundaries.

The provider fetches PNG tiles via HTTP (ixwebsocket HttpClient), decodes Terrarium-encoded elevation values using GDAL, and writes temporary GeoTIFFs with proper CRS and geotransform for the canonical ingestion pipeline. The `MockTerrainProvider` is retained for deterministic unit tests only and is not exposed as a production user option.

### HTTP cancellation

HTTP cancellation is interruptible throughout the synchronous request lifecycle where the transport library supports it:

- **DNS/connect:** A RAII watcher thread polls the application cancellation callback every 50ms and sets `args->cancel` (the `std::atomic<bool>` that ixwebsocket checks internally at every I/O boundary).
- **TLS/connect:** Same watcher thread mechanism.
- **Request write:** Same watcher thread mechanism.
- **Status/header waiting:** Same watcher thread mechanism.
- **Response transfer:** The `onProgressCallback` returns false when cancellation is requested, aborting the transfer.
- **Retries:** Cancellation is checked before each retry attempt.
- **Retry backoff:** The backoff loop checks cancellation at bounded intervals (100ms).

The watcher thread is exception-safe (RAII `WatcherJoinGuard`): it is always signalled and joined before `get()` returns, even if the HTTP call throws. No detached threads, no unsafe captures, no thread leaks. The generic `HttpClient` port contract requires that implementations support prompt cancellation throughout the synchronous request lifecycle where the transport library supports it.

### HTTP error classification

Transport-level errors are classified into a port-level `TransportError` enum. The mapping reflects what the underlying ixwebsocket library can genuinely tell us — no fake precision:

- `None` — HTTP exchange completed.
- `Cancelled` — Request was cancelled by the caller.
- `Timeout` — Connect or transfer timeout.
- `DnsFailure` — DNS resolution failure (only when the library specifically reports DNS failure).
- `ConnectionFailure` — TCP connect / connection reset / malformed URL (UrlMalformed is classified here, not as DnsFailure, because a malformed URL is not a DNS failure).
- `TlsFailure` — TLS handshake / certificate failure (only when the library distinguishes TLS specifically).
- `ProtocolFailure` — HTTP protocol error (malformed response).
- `UnknownNetworkFailure` — Network failure that doesn't fit above categories.

These map to terrain provider error semantics:

- HTTP 401/403 → `AuthenticationFailed`
- HTTP 429 → `RateLimited`
- HTTP 404/410 → `SourceUnavailable`
- HTTP 5xx → `SourceUnavailable` (retryable)
- `Timeout` → `NetworkTimeout`
- `DnsFailure`/`ConnectionFailure`/`TlsFailure`/`ProtocolFailure` → `SourceUnavailable`
- `Cancelled` → `Cancelled`

### Location search

Location search provides geographic repositioning for the Download Area map:

- **`LocationSearchClient`** — interface for search providers (includes `cancelPending()` for lifecycle management).
- **`LocationSearchConfig`** — endpoint, throttling, cache, attribution configuration.
- **`NominatimLocationSearchClient`** — browser fallback implementation using the Nominatim public API.
- **`DesktopLocationSearchClient`** — desktop implementation routing search via IPC to the Electron main process (`GeocoderService`).
- **`defaultTerrainSearchConfig`** — default configuration (public Nominatim endpoint, 1 req/s throttle, 32-entry cache).
- **`initTerrainConfig()` / `getTerrainSearchConfig()` / `setTerrainSearchConfig()`** — runtime configuration layer. Loaded from desktop runtime config, environment variables (`VITE_GEOCODER_ENDPOINT`), or packaged configuration without requiring rebuilds.

Search behavior and lifecycle:

- **Explicit search:** User enters a location, presses Search or Enter, and exactly one request is made. No autocomplete on every keystroke.
- **Centralized throttling:** Max 1 request per second (Nominatim usage policy). Enforced centrally in the desktop main process across the entire application, and per-client in web fallback.
- **Bounded LRU cache:** Normalized queries (whitespace/case) are cached with a configurable maximum (default 32 entries).
- **Clean cancellation & promise rejection:** When `cancelPending()` is called (e.g. on unmount or when a new search supersedes a pending search), pending promises are cleanly rejected with `SearchCancelledError`. The UI catches `SearchCancelledError` silently, preventing phantom empty results or spurious error alerts.
- **Explicit UX states:** Searching, no-results, error, results — no-results is visible, not silently an empty list. The searching indicator clears on both success and failure.
- **Attribution & Licensing:** `© OpenStreetMap contributors` is shown in the search UI. Results are licensed under the Open Data Commons Open Database License (ODbL) 1.0 by the OpenStreetMap Foundation (OSMF).
- **Stale response suppression:** A generation counter ensures stale responses from earlier queries are ignored.

Request identification and network identity:

- In Electron desktop execution, requests are routed to the main process via `ipcRenderer.invoke('geocoder:search', query)`. The main process sends an official, identifiable `User-Agent`: `InfraForge/0.3.0 (https://infraforge.app; contact@infraforge.app)` (or configurable via `INFRAFORGE_GEOCODER_USER_AGENT`). No fake `Referer` headers are used.
- In browser fallback, standard `fetch` is used with `Accept: application/json`.
- Search results are frontend UX only — they reposition the map and are NOT terrain truth.

Development fallback vs. production deployment:

- The public OpenStreetMap Nominatim service is a development fallback intended strictly for low-volume testing and interactive searches.
- For production enterprise deployment, a dedicated self-hosted Nominatim instance, Pelias, or commercial geocoding service (e.g. Geocode Earth, LocationIQ) should be configured via the runtime configuration layer (`INFRAFORGE_GEOCODER_ENDPOINT` or `VITE_GEOCODER_ENDPOINT`). Public Nominatim must not be relied upon for automated or high-volume workloads.

### OSM map tile endpoint and network identity

The Leaflet tile layer uses a configurable map tile provider (`TerrainMapTileConfig`):

- **`defaultTerrainMapTileConfig`** — default configuration:
  - URL: `https://tile.openstreetmap.org/{z}/{x}/{y}.png`
  - Attribution: `© OpenStreetMap contributors` (ODbL 1.0).
  - Max zoom: 19
- **Runtime configurability:** Loaded from the desktop bridge, environment (`VITE_TILE_URL` / `INFRAFORGE_TILE_URL`), or defaults.
- **Electron session network identity:** The Electron main process sets `session.defaultSession.setUserAgent()` and injects the official application identity for all outgoing tile requests.
- **Tile usage compliance:** No bulk downloading or pre-caching of map tiles is performed. Tile requests are strictly on-demand for Leaflet viewport display.

### Selection tile size semantics

The 1/2/4/8/16 km application selection tiles are **Web Mercator grid dimensions**, not physical ground-distance squares. At higher latitudes (>60°), the physical scale differs significantly — a "4 km tile" at 70° latitude is approximately 2 km in the east-west direction. This is acceptable because:

1. The grid is a project-area unit, not canonical terrain geometry.
2. The canonical terrain data uses real CRS via GDAL/PROJ.
3. The provider requests use the tile bounds in WGS84, not the WebMercator approximation.
4. The canonical transformation always uses GeoTransformService (PROJ).

The `areaSqm` shown in the plan is approximate (Web Mercator metres), not exact physical ground area.

### Plan identity

The Download Area UI uses an explicit plan identity to prevent stale plan races:

- The identity includes `providerId`, `tileSize`, `west`, `south`, `east`, `north`.
- When a new area is drawn, the previous plan is immediately invalidated (`setPlan(null)`) — no reliance on React effect cleanup timing.
- A delayed/stale response from an earlier request is ignored because its identity no longer matches the current inputs.
- The Download Selected button is only enabled if a plan exists, its identity matches the current provider/area/tileSize, and the selected indices are valid for that exact plan.

### Backend validation

The native engine rejects all invalid inputs with stable typed error codes:

- Project not open
- Unknown provider
- NaN/Infinity coordinates
- west >= east
- south >= north
- Longitude out of range [-180, 180]
- Latitude outside Web Mercator valid range [-85.05, 85.05]
- Invalid tile size (must be 1000, 2000, 4000, 8000, or 16000)
- Negative selected index
- Selected index >= grid size
- Duplicate selected index
- Empty selection for `startDownload`
- Oversized selection grid
- Too many provider requests
- Empty terrain name
- Overlong terrain name (> 256 characters)

## CRS flow

All source↔canonical conversion goes through `GeoTransformService` (ADR-0007) — terrain never calls PROJ directly from domain/application code, never invents local coordinate math, and never reduces to float before the render boundary.

Dataset coverage is stored in canonical project-global coordinates. Changing the project georeference while terrain datasets exist is rejected unless a future explicit migration/reprojection workflow is introduced.

## Canonical sampling (`terrain.sample` / `TerrainSampler`)

Double-precision, project-global in, canonical-linear-unit out:

- Bilinear interpolation over the four surrounding cell centers for raster-backed coverage.
- Any NoData cell in the interpolation stencil yields NoData according to the dataset policy — no fabricated replacement.
- Outside actual selected/imported coverage yields OutsideCoverage/no terrain.
- Height conversion follows the canonical linear-unit rule used by tile generation.
- Overlap resolution is deterministic; explicit dataset id pins a source.
- Sampling never reads Vulkan meshes; the renderer is not a height source.

For sparse/disconnected area downloads, the coverage mask/pieces are checked before raster sampling so an unselected gap cannot be sampled merely because it falls inside an enclosing raster extent.

## Tiling and the world partition

Renderer tiles are chunk-aligned derived data over **actual canonical terrain coverage**, not merely dataset bounding boxes. Each tile file is self-describing and carries dataset/revision provenance plus an LOD pyramid.

Heights are absolute canonical elevations; NoData positions remain NoData. Files publish atomically. The decoder validates schema/structure/provenance; stale or corrupt tiles are rejected rather than interpreted leniently.

Tile generation samples project-owned canonical source data. It must not generate geometry for unselected gaps in sparse area acquisitions.

## Renderer integration

`terrain.get_scene` projects derived scene metadata only: render origin, per-tile identity/revision/path/coverage. The desktop shell forwards that metadata to the viewport; the frontend never interprets native tile payloads and the renderer never queries SQLite.

`TerrainTileCache` owns GPU-independent streaming/LOD residency. LOD replacement keeps an old resident payload drawable while a replacement loads, and a failed replacement keeps the old working payload rather than dropping visible terrain.

`TerrainPass` renders depth-tested heightfields. Double→float conversion happens at the shared `RenderLocalFrame` boundary. NoData produces holes rather than fabricated surfaces. Vulkan uploads use host-visible staging followed by device-local vertex/index buffers; non-coherent flushes must obey Vulkan `nonCoherentAtomSize` rules. Empty/all-NoData render payloads must not create zero-size Vulkan buffers.

## Diagnostics

Typed terrain diagnostics/errors cover at minimum missing/unsupported CRS, unsupported raster orientation/sample configuration, corrupt source, invalid coverage, NoData presence, missing project-owned source storage, failed tile generation, and remote acquisition/provider failures.

Diagnostics are backend-observed facts and flow through the canonical Problems projection. Nothing is fabricated by the frontend.

## Threading and consistency

Canonical mutations are owned by the application executor. Worker tasks use immutable snapshots and worker-confined transform state. Shutdown must not call executor-owned TerrainService/ProjectStore mutation concurrently from another thread; worker shutdown, executor shutdown, and completion callback dropping must have deterministic race-free ordering.

Canonical source storage and the persisted dataset row form one consistency boundary. A failure after database commit cannot delete the only source file while leaving the row behind. Derived-work/event failures are handled separately from canonical commit failure.

## Verification

Issue #6 requires both automated and manual gates where applicable.

### Automated local-file coverage

- real deterministic GeoTIFF fixtures;
- probe/import/reopen/sampling control points;
- NoData behavior;
- cancellation/cleanup;
- canonical commit failure compensation;
- derived tile generation and scene projection;
- protocol/event/job lifecycle;
- Windows + Linux native CI;
- frontend/desktop typecheck/tests/build.

### Automated area-download coverage

- deterministic 1/2/4/8/16 km selection grid;
- tile toggle/select-all/clear;
- disconnected selection;
- provider request planning/deduplication;
- only required provider requests made;
- unselected gaps absent from canonical coverage/sampling/renderer scene;
- bounded retry and typed failure mapping;
- cancellation cleanup;
- reopen without contacting provider;
- deterministic mock/local provider responses only.

### Manual acceptance

When a Vulkan-capable desktop and real provider access are available, validate real area selection/download, real DEM rendering, LOD changes, sparse selection gaps, reopen, sampling, and Vulkan validation layers. If this has not been run, documentation/PR reports must say so explicitly.

## Limitations

The `Download Area` selective remote acquisition path is implemented with a real production terrain DEM provider (AWS Terrain Tiles / Terrarium). Manual runtime verification (real HTTP download, real Vulkan rendering, real sampling) is pending. The automated test suite covers provider planning, dedup, error handling, and the full assembly pipeline using deterministic mock HTTP fixtures.

Other known scope limits:

- one terrain worker queue rather than a general worker pool;
- eager derived-tile generation may be bounded for very large coverage;
- in-place elevation editing/re-ingest and dataset removal may remain follow-up work unless separately required;
- overlapping datasets use deterministic priority/explicit dataset selection;
- viewport camera may remain orthographic top-down until later renderer work.
