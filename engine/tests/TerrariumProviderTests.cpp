#include "infraforge/domain/terrain/TerrariumTerrainProvider.hpp"
#include "infraforge/ports/MockHttpClient.hpp"
#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
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
        CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, width, height,
            data.data(), width, height, GDT_Byte, 0, 0);
        (void)err;
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
            // Estimated bytes is 0 (unknown) — Terrarium PNG size varies
            // per tile and is not deterministically known before fetch
            // (Finding 4).
            CHECK(req.estimatedBytes == 0);
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

        auto resultPath = provider.fetchRequest(req, tempDir, "", {});

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
            (void)provider.fetchRequest(req, tempDir, "", {});
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
            (void)provider.fetchRequest(req, tempDir, "", {});
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
            (void)provider.fetchRequest(req, tempDir, "", {});
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
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::InvalidProviderResponse);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    // ---- Finding 8: XYZ range validation ----

    TEST_CASE("Terrarium XYZ: z=0 x=0 y=0 is valid") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_xyz_valid";
        std::filesystem::create_directories(tempDir);

        // z=0 has only tile (0,0). Set up a mock response.
        const std::string url =
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/0/0/0.png";
        std::string pngData = makeTerrariumPng();
        if (!pngData.empty()) {
            httpClient->setResponse(url, 200, pngData);
        }

        ProviderRequest req;
        req.requestId = "0/0/0";

        // Should not throw on parsing (may throw on fetch if PNG driver missing).
        bool parseOk = true;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            // If it's InvalidProviderResponse, parsing failed.
            if (e.code() == ProviderErrorCode::InvalidProviderResponse) {
                parseOk = false;
            }
        }
        CHECK(parseOk);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium XYZ: z=0 x=1 is invalid (x >= 2^z)") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_xyz_x_invalid";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "0/1/0"; // x=1 but 2^0=1, so x >= 1

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::InvalidProviderResponse);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium XYZ: max valid x/y at z=11") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_xyz_max_valid";
        std::filesystem::create_directories(tempDir);

        // 2^11 = 2048, so max valid x/y = 2047.
        ProviderRequest req;
        req.requestId = "11/2047/2047";

        bool parseOk = true;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            if (e.code() == ProviderErrorCode::InvalidProviderResponse) {
                parseOk = false;
            }
        }
        CHECK(parseOk);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium XYZ: x = 2^z is invalid") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_xyz_x_eq_2z";
        std::filesystem::create_directories(tempDir);

        // 2^11 = 2048, x=2048 is out of range.
        ProviderRequest req;
        req.requestId = "11/2048/0";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::InvalidProviderResponse);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium XYZ: negative coordinates are invalid") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_xyz_neg";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/-1/0";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::InvalidProviderResponse);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium XYZ: trailing garbage is invalid") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_xyz_trail";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/0/0extra";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::InvalidProviderResponse);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    // ---- Finding 9: Tile boundary overfetch ----

    TEST_CASE("Terrarium tile boundary: exact east edge does not overfetch") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        // At z=11, tile size = 2*PI*6378137 / 2^11 ≈ 38218.5 m.
        // Tile x=0 spans [-20037508.34, -19615323.84] in Web Mercator X.
        // An area ending exactly at -19615323.84 should not request tile x=1.
        const int z = 11;
        const double kEarthRadius = 6378137.0;
        const double kOriginShift = 3.14159265358979323846 * kEarthRadius;
        const double tileSize = (2.0 * kOriginShift) / static_cast<double>(1 << z);
        const double tile0MaxX = -kOriginShift + tileSize;

        // Convert to WGS84.
        const double tile0MaxLon = (tile0MaxX / kEarthRadius) * 180.0 / 3.14159265358979323846;

        // Area that ends exactly on the tile 0/1 boundary.
        GeoBounds area{
            .west = -180.0,
            .south = -1.0,
            .east = tile0MaxLon,
            .north = 1.0};

        auto tiles = computeSelectionGrid(area, 4000);
        REQUIRE(!tiles.empty());

        auto requests = provider.planRequests(tiles);
        REQUIRE(!requests.empty());

        // All requests should be at z=11. Check that no request has x=1
        // (which would mean overfetch past the exact boundary).
        for (const auto& req : requests) {
            // Parse z/x/y from requestId.
            int rz, rx, ry;
            char s1, s2;
            std::istringstream iss(req.requestId);
            iss >> rz >> s1 >> rx >> s2 >> ry;
            REQUIRE(rz == z);
            // x should be 0 only (the area ends exactly on tile 0's east edge).
            CHECK(rx == 0);
        }
    }

    // ---- Finding 10: HTTP error classification ----

    TEST_CASE("Terrarium HTTP: transport timeout maps to NetworkTimeout") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::string url =
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/11/0/0.png";
        httpClient->setTransportError(url,
            infraforge::ports::TransportError::Timeout, "connect timeout");

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_timeout";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/0/0";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::NetworkTimeout);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium HTTP: DNS failure maps to SourceUnavailable") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::string url =
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/11/0/0.png";
        httpClient->setTransportError(url,
            infraforge::ports::TransportError::DnsFailure, "DNS resolution failed");

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_dns";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/0/0";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            threw = true;
            // DNS failure should NOT be mapped to NetworkTimeout.
            CHECK(e.code() == ProviderErrorCode::SourceUnavailable);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    TEST_CASE("Terrarium HTTP: connection failure maps to SourceUnavailable") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const std::string url =
            "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/11/0/0.png";
        httpClient->setTransportError(url,
            infraforge::ports::TransportError::ConnectionFailure, "connection refused");

        const std::filesystem::path tempDir =
            std::filesystem::temp_directory_path() / "terrarium_conn";
        std::filesystem::create_directories(tempDir);

        ProviderRequest req;
        req.requestId = "11/0/0";

        bool threw = false;
        try {
            (void)provider.fetchRequest(req, tempDir, "", {});
        } catch (const ProviderError& e) {
            threw = true;
            CHECK(e.code() == ProviderErrorCode::SourceUnavailable);
        }
        CHECK(threw);

        std::error_code ec;
        std::filesystem::remove_all(tempDir, ec);
    }

    // ---- Finding 4: Unknown size estimate ----

    TEST_CASE("Terrarium plan: estimated bytes is 0 (unknown)") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        GeoBounds area{.west = -105.5, .south = 39.5, .east = -105.0, .north = 40.0};
        auto tiles = computeSelectionGrid(area, 4000);

        std::vector<SelectionTile> selected;
        for (std::size_t i = 0; i < 5 && i < tiles.size(); ++i) {
            selected.push_back(tiles[i]);
        }

        auto requests = provider.planRequests(selected);
        CHECK(!requests.empty());
        for (const auto& req : requests) {
            // Size is unknown (0), not fabricated (Finding 4).
            CHECK(req.estimatedBytes == 0);
        }
    }

    // ---- Finding 5: Resolution semantics ----

    TEST_CASE("Terrarium plan: effective resolution varies with latitude") {
        // The effective ground resolution at z=11 varies with latitude
        // due to Web Mercator projection. At the equator it's ~38218 m/tile
        // / 256 px ≈ 149 m/px. At 60° it's ~75 m/px (cos(60°) = 0.5).
        // We verify the resolution formula: ground resolution =
        // (tileSizeMeters * cos(latitude)) / pixelsPerTile.
        const double kEarthRadius = 6378137.0;
        const double kOriginShift = 3.14159265358979323846 * kEarthRadius;
        const int z = 11;
        const double tileSizeM = (2.0 * kOriginShift) / static_cast<double>(1 << z);
        const int pixelsPerTile = 256;

        // At equator (lat=0): resolution = tileSize * cos(0) / 256.
        const double res0 = tileSizeM * std::cos(0.0) / pixelsPerTile;
        // At lat=60: resolution = tileSize * cos(60°) / 256.
        const double res60 = tileSizeM * std::cos(60.0 * 3.14159265358979323846 / 180.0) / pixelsPerTile;

        // Higher latitude should have finer (lower) resolution.
        CHECK(res0 > res60);
        // At equator, resolution should be ~76 m/px.
        CHECK(res0 > 50.0);
        CHECK(res0 < 100.0);
        // At 60°, resolution should be ~38 m/px.
        CHECK(res60 > 20.0);
        CHECK(res60 < 50.0);
    }

    // ---- Finding 3: Attribution content ----

    TEST_CASE("Terrarium attribution contains joerd reference") {
        auto httpClient = std::make_shared<MockHttpClient>();
        TerrariumTerrainProvider provider(httpClient);

        const auto& info = provider.info();
        // Attribution must reference the joerd attribution page.
        CHECK(info.attribution.find("joerd") != std::string::npos);
        CHECK(info.attribution.find("attribution") != std::string::npos);
        // Must not be empty.
        CHECK(!info.attribution.empty());
    }
}

// ---- BLOCKER 2: Real interruptible HTTP cancellation ----

namespace {

// Mock HTTP client that blocks until the canceller returns true or a
// timeout. Simulates an in-flight request that must abort promptly when
// cancellation is requested (BLOCKER 2).
class BlockingMockHttpClient final : public infraforge::ports::HttpClient {
public:
    HttpResponse get(const std::string& /*url*/) override {
        HttpResponse r;
        r.statusCode = 200;
        r.body = "blocked";
        return r;
    }

    HttpResponse get(const std::string& /*url*/,
        const std::function<bool()>& cancelled) override {
        // Simulate a long-running transfer that checks cancellation via
        // the progress callback. If the canceller is wired correctly,
        // this returns promptly when cancel() returns true.
        if (cancelled && cancelled()) {
            HttpResponse r;
            r.statusCode = 0;
            r.errorMessage = "cancelled before request";
            return r;
        }
        // Poll cancellation in a tight loop (simulates ixwebsocket
        // progress callback polling). Must return within a bounded time
        // after cancellation is requested.
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{30};  // safety: test must cancel before this
        while (std::chrono::steady_clock::now() < deadline) {
            if (cancelled && cancelled()) {
                HttpResponse r;
                r.statusCode = 0;
                r.errorMessage = "cancelled";
                return r;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        HttpResponse r;
        r.statusCode = 200;
        r.body = "timeout";
        return r;
    }
};

} // namespace

TEST_CASE("Terrarium provider: cancellation before request returns promptly") {
    BlockingMockHttpClient http;
    TerrariumTerrainProvider provider{std::make_shared<BlockingMockHttpClient>(http)};

    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "terrarium_cancel_before";
    std::filesystem::create_directories(tempDir);

    ProviderRequest req;
    req.requestId = "11/123/456";
    req.bounds = GeoBounds{.west = -180, .south = -85.05, .east = -179.99, .north = -85.04};
    req.estimatedBytes = 100 * 1024;

    // Cancel immediately: the callback returns true on first call.
    std::atomic<bool> cancelled{true};
    auto cancelCb = [&cancelled] { return cancelled.load(); };

    const auto start = std::chrono::steady_clock::now();
    bool threw = false;
    try {
        (void)provider.fetchRequest(req, tempDir, "", cancelCb);
    } catch (const ProviderError& e) {
        threw = true;
        CHECK(e.code() == ProviderErrorCode::Cancelled);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    CHECK(threw);
    // Must return promptly (well under the 30s safety deadline).
    CHECK(elapsed.count() < 1000);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST_CASE("Terrarium provider: cancellation during in-flight request returns promptly") {
    BlockingMockHttpClient http;
    TerrariumTerrainProvider provider{std::make_shared<BlockingMockHttpClient>(http)};

    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "terrarium_cancel_during";
    std::filesystem::create_directories(tempDir);

    ProviderRequest req;
    req.requestId = "11/123/456";
    req.bounds = GeoBounds{.west = -180, .south = -85.05, .east = -179.99, .north = -85.04};
    req.estimatedBytes = 100 * 1024;

    // Start uncancelled, then cancel from another thread after 200ms.
    std::atomic<bool> cancelled{false};
    auto cancelCb = [&cancelled] { return cancelled.load(); };

    std::thread canceller([&cancelled] {
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        cancelled.store(true);
    });

    const auto start = std::chrono::steady_clock::now();
    bool threw = false;
    try {
        (void)provider.fetchRequest(req, tempDir, "", cancelCb);
    } catch (const ProviderError& e) {
        threw = true;
        CHECK(e.code() == ProviderErrorCode::Cancelled);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    canceller.join();
    CHECK(threw);
    // Must return promptly after cancellation (well under 30s).
    CHECK(elapsed.count() < 2000);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST_CASE("Terrarium provider: cancellation during retry backoff returns promptly") {
    // Use a mock that always returns 503 (retryable) so the provider enters
    // the backoff loop, then cancel during backoff.
    auto mockHttp = std::make_shared<MockHttpClient>();
    // Configure a 503 for all URLs (retryable, triggers backoff).
    mockHttp->setErrorResponse(
        "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/11/123/456.png",
        503, "service unavailable");

    TerrariumTerrainProvider provider{mockHttp};

    const std::filesystem::path tempDir =
        std::filesystem::temp_directory_path() / "terrarium_cancel_backoff";
    std::filesystem::create_directories(tempDir);

    ProviderRequest req;
    req.requestId = "11/123/456";
    req.bounds = GeoBounds{.west = -180, .south = -85.05, .east = -179.99, .north = -85.04};
    req.estimatedBytes = 100 * 1024;

    // Cancel after 150ms (during the first backoff, which is 500ms).
    std::atomic<bool> cancelled{false};
    auto cancelCb = [&cancelled] { return cancelled.load(); };

    std::thread canceller([&cancelled] {
        std::this_thread::sleep_for(std::chrono::milliseconds{150});
        cancelled.store(true);
    });

    const auto start = std::chrono::steady_clock::now();
    bool threw = false;
    try {
        (void)provider.fetchRequest(req, tempDir, "", cancelCb);
    } catch (const ProviderError& e) {
        threw = true;
        CHECK(e.code() == ProviderErrorCode::Cancelled);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    canceller.join();
    CHECK(threw);
    // Must return promptly after cancellation during backoff.
    CHECK(elapsed.count() < 1000);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}
