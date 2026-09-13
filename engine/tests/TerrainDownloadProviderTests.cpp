#include "infraforge/domain/terrain/MockTerrainProvider.hpp"
#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

using namespace infraforge::domain::terrain;

TEST_SUITE("terrain download provider") {

    TEST_CASE("computeSelectionGrid produces deterministic grid") {
        GeoBounds area{.west = -105.5, .south = 39.5, .east = -105.0, .north = 40.0};
        const auto tiles = computeSelectionGrid(area, 4000);
        CHECK(!tiles.empty());
        // The area is ~40km x ~55km, so at 4km tiles we expect ~10x14 = 140 tiles.
        // Allow some variance due to WebMercator projection.
        CHECK(tiles.size() > 100);
        CHECK(tiles.size() < 400);

        // Verify grid is deterministic: same input produces same output.
        const auto tiles2 = computeSelectionGrid(area, 4000);
        CHECK(tiles.size() == tiles2.size());
        CHECK(tiles[0].col == tiles2[0].col);
        CHECK(tiles[0].row == tiles2[0].row);
    }

    TEST_CASE("computeSelectionGrid rejects invalid tile sizes") {
        GeoBounds area{.west = -1, .south = -1, .east = 1, .north = 1};
        bool threw = false;
        try { (void)computeSelectionGrid(area, 500); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { (void)computeSelectionGrid(area, 3000); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { (void)computeSelectionGrid(area, 32000); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }

    TEST_CASE("computeSelectionGrid handles empty area") {
        GeoBounds empty{};
        const auto tiles = computeSelectionGrid(empty, 4000);
        CHECK(tiles.empty());
    }

    TEST_CASE("computeSelectionGrid tile bounds are within area") {
        GeoBounds area{.west = -105.5, .south = 39.5, .east = -105.0, .north = 40.0};
        const auto tiles = computeSelectionGrid(area, 8000);
        for (const auto& tile : tiles) {
            CHECK(tile.bounds.west >= area.west - 0.001);
            CHECK(tile.bounds.east <= area.east + 0.001);
            CHECK(tile.bounds.south >= area.south - 0.001);
            CHECK(tile.bounds.north <= area.north + 0.001);
        }
    }

    TEST_CASE("mock provider planRequests deduplicates shared tiles") {
        MockTerrainProvider provider;
        // Two adjacent application tiles that share the same XYZ tile at zoom 10.
        SelectionTile t1;
        t1.bounds = {.west = -105.01, .south = 39.99, .east = -105.0, .north = 40.0};
        SelectionTile t2;
        t2.bounds = {.west = -105.02, .south = 39.98, .east = -105.01, .north = 39.99};
        const auto requests = provider.planRequests({t1, t2});
        // The requests should be deduplicated (no duplicate request IDs).
        std::set<std::string> ids;
        for (const auto& req : requests) {
            ids.insert(req.requestId);
        }
        CHECK(ids.size() == requests.size());
    }

    TEST_CASE("mock provider planRequests only covers selected tiles") {
        MockTerrainProvider provider;
        // Select only 3 tiles out of a larger area.
        std::vector<SelectionTile> selected;
        for (int i = 0; i < 3; ++i) {
            SelectionTile t;
            t.bounds = {
                .west = -105.0 + i * 0.01,
                .south = 39.5,
                .east = -105.0 + (i + 1) * 0.01,
                .north = 39.51};
            selected.push_back(t);
        }
        const auto requests = provider.planRequests(selected);
        CHECK(!requests.empty());
        // The number of requests should be small (covering only 3 small tiles).
        CHECK(requests.size() < 20);
    }

    TEST_CASE("mock provider fetchRequest writes deterministic file") {
        GDALAllRegister();
        MockTerrainProvider provider;
        ProviderRequest req;
        req.requestId = "10/163/395";
        req.bounds = {.west = -105.0, .south = 39.5, .east = -104.9, .north = 39.6};
        req.estimatedBytes = 256 * 256 * 4;

        const auto tempDir = std::filesystem::temp_directory_path() / "infraforge_test_mock";
        std::filesystem::create_directories(tempDir);
        const auto file = provider.fetchRequest(req, tempDir, "");
        CHECK(std::filesystem::exists(file));
        CHECK(std::filesystem::file_size(file) > 0);

        // Verify the file is a GDAL-readable GeoTIFF with CRS and geotransform
        // (BLOCKER 2 regression: mock provider must write real GeoTIFFs, not
        // the old MDEM binary format).
        {
            GDALDatasetH ds = GDALOpen(file.string().c_str(), GA_ReadOnly);
            CHECK(ds != nullptr);
            if (ds) {
                CHECK(GDALGetRasterXSize(ds) > 0);
                CHECK(GDALGetRasterYSize(ds) > 0);
                // CRS must be set (non-empty WKT). The mock provider sets
                // EPSG:3857; the full pipeline test verifies the authority
                // code is resolved correctly.
                const char* projWkt = GDALGetProjectionRef(ds);
                CHECK(projWkt != nullptr);
                CHECK(std::string{projWkt}.size() > 0);
                double geotransform[6] = {0};
                GDALGetGeoTransform(ds, geotransform);
                CHECK(geotransform[1] > 0.0);  // pixel width positive
                CHECK(geotransform[5] < 0.0);  // pixel height negative
                GDALClose(ds);
            }
        }

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("mock provider fail mode throws ProviderError") {
        MockTerrainProvider provider;
        provider.setFailMode(ProviderErrorCode::AuthenticationFailed);
        CHECK(provider.shouldFail());

        ProviderRequest req;
        req.requestId = "10/0/0";
        req.bounds = {.west = 0, .south = 0, .east = 0.1, .north = 0.1};
        const auto tempDir = std::filesystem::temp_directory_path() / "infraforge_test_fail";
        std::filesystem::create_directories(tempDir);
        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "");
        } catch (const ProviderError&) {
            threw = true;
        }
        CHECK(threw);
        std::filesystem::remove_all(tempDir);
    }

    TEST_CASE("provider registry lists and finds providers") {
        TerrainProviderRegistry registry;
        registry.registerProvider(std::make_unique<MockTerrainProvider>());
        const auto providers = registry.listProviders();
        CHECK(providers.size() == 1);
        CHECK(providers[0].providerId == "mock-terrain");

        const auto* found = registry.find("mock-terrain");
        CHECK(found != nullptr);
        CHECK(found->info().providerId == "mock-terrain");

        const auto* notFound = registry.find("nonexistent");
        CHECK(notFound == nullptr);
    }

    TEST_CASE("selection tiles are separate from provider request tiles") {
        // TerrainSelectionTile (application-level) uses col/row indices in
        // a project grid. Provider requests use provider-specific tile
        // schemes (e.g. z/x/y). The domain model keeps them separate.
        MockTerrainProvider provider;
        SelectionTile appTile;
        appTile.col = 5;
        appTile.row = 10;
        appTile.bounds = {.west = -105.0, .south = 39.5, .east = -104.9, .north = 39.6};
        appTile.areaSqm = 1000000.0;

        const auto requests = provider.planRequests({appTile});
        // Provider requests have request IDs in z/x/y format, not col/row.
        for (const auto& req : requests) {
            CHECK(!req.requestId.empty());
            CHECK(req.requestId.find('/') != std::string::npos);
            // The request ID is in z/x/y format, not the application tile's col/row.
        }
    }
}
