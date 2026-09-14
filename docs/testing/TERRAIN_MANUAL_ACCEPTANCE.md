# Terrain manual acceptance

Use a deterministic physical DEM, not an unlabelled normalized heightmap.

## Acceptance DEM

The canonical test generator is `engine/tests/TerrainTestFixtures.hpp` (`writeDemGeoTiff`). It creates a 32 × 32 GeoTIFF in EPSG:32633 with 10 m pixels, an explicitly declared metre elevation unit, and the surface:

`height(row, column) = 100 + 0.5 × column + 0.25 × row metres`

Coverage is 500000–500320 E and 4650000–4650320 N. Control samples include 100.00 m at row 0/column 0 and 123.25 m at row 31/column 31. Tests may include NoData at row 5/column 5. The fixture is generated through GDAL during the native test suite so no opaque binary is committed.

## Source-tree developer run

1. Configure and build Release native binaries with warnings as errors.
2. Run `npm run start:desktop` from the repository root. This command rebuilds the protocol, frontend, and desktop output before Electron starts, preventing stale `frontend/dist` testing.
3. If binaries are outside the standard source build, set `INFRAFORGE_ENGINE_PATH` and `INFRAFORGE_VIEWPORT_PATH` explicitly.
4. Open Download Area and record the provider, resolved URL, config source, build marker, and first tile lifecycle shown below the map.
5. Complete Local File, Download Area, cancellation, save/reopen, source-removed, and offline-reopen checks from PR #28's RC checklist. Record observed HTTP status and viewport diagnostics; automated tests are not a substitute for these steps.

## Packaged RC acceptance run

1. Install or unpack the Windows RC artifact.
2. Clear `INFRAFORGE_ENGINE_PATH` and `INFRAFORGE_VIEWPORT_PATH`.
3. Confirm the shell discovers both executables from packaged `resources/native` and reaches engine/renderer ready states.
4. Repeat the complete RC checklist using the packaged application.

No packaged RC artifact is produced by the repository yet. Packaged acceptance remains blocked until a packaging workflow copies the Release engine, viewport, GDAL/PROJ runtime libraries, and GDAL/PROJ data into the application resources and emits an installer/archive.
