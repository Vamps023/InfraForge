#include "infraforge/domain/terrain/TerrariumTerrainProvider.hpp"
#include "infraforge/ports/MockHttpClient.hpp"
#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"

#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gdal.h>
#include <gdal_priv.h>

using namespace infraforge::domain::terrain;
using infraforge::ports::MockHttpClient;
using infraforge::ports::HttpResponse;

namespace {

// Create a minimal valid Terrarium PNG (256x256, 3 bands).
// All pixels encode elevation 0: (R=128, G=0, B=0) → (128*256 + 0 + 0/256) - 32768 = 0
std::string makeTerrariumPng(int width = 256, int height = 256) {
    // Use GDAL to create a PNG file on disk, then read it back.
    GDALDriverH memDriver = GDALGetDriverByName("MEM");
    if (!memDriver) return "";

    GDALDatasetH ds = GDALCreate(memDriver, "mem", width, height, 3, GDT_Byte, nullptr);
    if (!ds) return "";

    // Fill all bands with 128 (R), 0 (G), 0 (B) → elevation 0
    for (int b = 1; b <= 3; ++b) {
        GDALRasterBandH band = GDALGetRasterBand(ds, b);
        std::vector<uint8_t> data(width * height, b == 1 ? 128 : 0);
        GDALRasterIO(band, GF_Write, 0, 0, width, height,
            data.data(), width, height, GDT_Byte, 0, 0);
    }

    // Write to a temp file as PNG.
    GDALDriverH pngDriver = GDALGetDriverByName("PNG");
    if (!pngDriver) {
        GDALClose(ds);
        return "";
    }

    static int counter = 0;
    std::string tempPath = (std::filesystem::temp_directory_path() /
        ("test_terrarium_" + std::to_string(counter++) + ".png")).string();

    const char* options[] = { nullptr };
    GDALDatasetH outDs = GDALCreateCopy(pngDriver, tempPath.c_str(), ds, FALSE, options, nullptr, nullptr);
    GDALClose(ds);
    if (!outDs) return "";
    GDALClose(outDs);

    // Read the PNG data from the file.
    std::ifstream in(tempPath, std::ios::binary);
    std::string result((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    std::error_code ec;
    std::filesystem::remove(tempPath, ec);
    return result;
}

} // namespace

TEST_SUITE("terrarium terrain provider") {

    TEST_CASE("Terrarium provider info is correct") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const auto& info = provider.info();
        CHECK(info.providerId == "terrarium-aws");
        CHECK(info.displayName.find("AWS Terrain Tiles") != std::string::npos);
        CHECK(!info.attribution.empty());
        CHECK(info.requiresAuth == false);
        CHECK(info.coverage.isEmpty());  // Global coverage (empty = global)
    }

    TEST_CASE("Terrarium provider supports selective requests") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);
        CHECK(provider.supportsSelectiveRequests());
    }

    TEST_CASE("Terrarium provider plans requests for selected tiles") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        // Create a small selection grid.
        GeoBounds area{.west = -105.5, .south = 39.5, .east = -105.0, .north = 40.0};
        auto tiles = computeSelectionGrid(area, 4000);

        // Select only a few tiles.
        std::vector<SelectionTile> selected;
        for (std::size_t i = 0; i < 5 && i < tiles.size(); ++i) {
            selected.push_back(tiles[i]);
        }

        auto requests = provider.planRequests(selected);
        CHECK(!requests.empty());
        // Each request should have a valid z/x/y ID.
        for (const auto& req : requests) {
            CHECK(!req.requestId.empty());
            CHECK(req.estimatedBytes > 0);
        }
    }

    TEST_CASE("Terrarium provider deduplicates overlapping tiles") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        GeoBounds area{.west = -105.5, .south = 39.5, .east = -105.0, .north = 40.0};
        auto tiles = computeSelectionGrid(area, 4000);

        // Select adjacent tiles that share provider tiles.
        std::vector<SelectionTile> selected;
        for (std::size_t i = 0; i < 10 && i < tiles.size(); ++i) {
            selected.push_back(tiles[i]);
        }

        auto requests = provider.planRequests(selected);
        // Verify no duplicate request IDs.
        std::set<std::string> ids;
        for (const auto& req : requests) {
            CHECK(ids.find(req.requestId) == ids.end());
            ids.insert(req.requestId);
        }
    }

    TEST_CASE("Terrarium provider fetches and decodes a tile") {
        // Skip if the PNG driver is not available in this GDAL build.
        GDALDriverH pngDriver = GDALGetDriverByName("PNG");
        if (!pngDriver) {
            MESSAGE("PNG driver not available — skipping fetch/decode test");
            return;
        }

        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        // Create a mock Terrarium PNG response.
        std::string pngData = makeTerrariumPng();
        REQUIRE(!pngData.empty());

        // Set up the mock HTTP response for a specific tile URL.
        const std::string url =
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/11/0/0.png";
        httpClient->setResponse(url, 200, pngData);

        // Create a temp directory.
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "terrarium_test";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/0/0";
        req.bounds = GeoBounds{.west = -180, .south = -85.05, .east = -179.99, .north = -85.04};
        req.estimatedBytes = 100 * 1024;

        auto resultPath = provider.fetchRequest(req, tempDir, "");

        // Verify the GeoTIFF was created.
        CHECK(std::filesystem::exists(resultPath));
        CHECK(std::filesystem::file_size(resultPath) > 0);

        // Verify the GeoTIFF is readable by GDAL.
        GDALDatasetH ds = GDALOpen(resultPath.string().c_str(), GA_ReadOnly);
        CHECK(ds != nullptr);
        if (ds) {
            CHECK(GDALGetRasterXSize(ds) == 256);
            CHECK(GDALGetRasterYSize(ds) == 256);
            CHECK(GDALGetRasterCount(ds) == 1);

            // Check CRS is EPSG:3857.
            const char* projWkt = GDALGetProjectionRef(ds);
            CHECK(projWkt != nullptr);
            const std::string wktStr(projWkt);
            const bool has3857 = wktStr.find("3857") != std::string::npos;
            const bool hasMercator = wktStr.find("Mercator") != std::string::npos;
            const bool hasValidCrs = has3857 || hasMercator;
            CHECK(hasValidCrs);

            // Check geotransform.
            double geotransform[6];
            GDALGetGeoTransform(ds, geotransform);
            CHECK(geotransform[1] > 0.0);  // Pixel width positive
            CHECK(geotransform[5] < 0.0);  // Pixel height negative

            // Check NoData.
            GDALRasterBandH band = GDALGetRasterBand(ds, 1);
            int hasNodata = 0;
            double nodata = GDALGetRasterNoDataValue(band, &hasNodata);
            CHECK(hasNodata == 1);
            CHECK(nodata == -32768.0);

            GDALClose(ds);
        }

        // Cleanup.
        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium provider handles HTTP 404") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::string url =
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/11/0/0.png";
        httpClient->setErrorResponse(url, 404, "Not Found");

        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "terrarium_404";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/0/0";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "");
        } catch (const ProviderError& e) {
            threw = true;
            // 404 maps to SourceUnavailable, which is retryable, but after
            // max retries it should still throw.
            CHECK(e.code() == ProviderErrorCode::SourceUnavailable);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium provider handles HTTP 401 auth error") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::string url =
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/11/0/0.png";
        httpClient->setErrorResponse(url, 401, "Unauthorized");

        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "terrarium_401";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/0/0";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "");
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::AuthenticationFailed);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium provider handles HTTP 429 rate limit") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::string url =
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/11/0/0.png";
        httpClient->setErrorResponse(url, 429, "Too Many Requests");

        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "terrarium_429";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/0/0";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "");
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::RateLimited);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium provider handles malformed request ID") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "terrarium_malformed";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "not-a-valid-id";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "");
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::InvalidProviderResponse);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }
}
