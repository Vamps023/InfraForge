#include <doctest/doctest.h>

#include "infraforge/application/TerrainExportEngine.hpp"
#include "infraforge/domain/terrain/TerrainDataset.hpp"
#include "infraforge/domain/terrain/TerrainTileFile.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "TerrainTestFixtures.hpp"

#include <array>
#include <cstring>
#include <stdexcept>

TEST_SUITE("terrain domain") {

TEST_CASE("dataset validation rejects structurally invalid records") {
    using infraforge::domain::terrain::TerrainDataset;

    TerrainDataset dataset;
    dataset.id = infraforge::domain::terrain::entityIdFromUuidText(
        "12345678-1234-5678-1234-567812345678");
    dataset.displayName = "Valid DEM";
    dataset.storagePath = "terrain/elevation/12345678-1234-5678-1234-567812345678.tif";
    dataset.sourceFormat = "GTiff";
    dataset.sourceCrs = "EPSG:32633";
    dataset.rasterWidth = 32;
    dataset.rasterHeight = 32;
    dataset.cellSizeX = 10.0;
    dataset.cellSizeY = 10.0;
    dataset.elevationUnit = "metre";
    dataset.elevationUnitToMetre = 1.0;
    dataset.bounds = infraforge::domain::world::SpatialBounds::ofEdges(500000.0, 4650000.0, 500320.0, 4650320.0);
    dataset.sourceSha256 = std::string(64, 'a');
    dataset.sourceBytes = 1024;
    dataset.revision = 1;

    CHECK(infraforge::domain::terrain::validateTerrainDataset(dataset) == std::nullopt);

    auto invalid = dataset;
    invalid.id = infraforge::domain::world::EntityId{};
    CHECK(infraforge::domain::terrain::validateTerrainDataset(invalid).has_value());

    invalid = dataset;
    invalid.sourceCrs.clear();
    CHECK(infraforge::domain::terrain::validateTerrainDataset(invalid).has_value());

    invalid = dataset;
    invalid.cellSizeX = -1.0;
    CHECK(infraforge::domain::terrain::validateTerrainDataset(invalid).has_value());

    invalid = dataset;
    invalid.storagePath = "/absolute/path.tif";
    CHECK(infraforge::domain::terrain::validateTerrainDataset(invalid).has_value());

    invalid = dataset;
    invalid.storagePath = "../escape.tif";
    CHECK(infraforge::domain::terrain::validateTerrainDataset(invalid).has_value());

    invalid = dataset;
    invalid.sourceSha256 = "tooshort";
    CHECK(infraforge::domain::terrain::validateTerrainDataset(invalid).has_value());
}

TEST_CASE("dataset identity maps uuid text <-> entity id") {
    const std::string uuid = "0f1e2d3c-4b5a-6978-8798-aabbccddeeff";
    const auto id = infraforge::domain::terrain::entityIdFromUuidText(uuid);
    CHECK_FALSE(id.isNull());
    CHECK(infraforge::domain::terrain::uuidTextFromEntityId(id) == uuid);
}

TEST_CASE("terrain error codes round-trip through stable names") {
    for (std::size_t code = 0;
        code <= static_cast<std::size_t>(infraforge::domain::terrain::TerrainErrorCode::InvalidArgument);
        ++code) {
        const auto value = static_cast<infraforge::domain::terrain::TerrainErrorCode>(code);
        const auto parsed =
            infraforge::domain::terrain::terrainErrorCodeFromName(
                infraforge::domain::terrain::terrainErrorCodeName(value));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == value);
    }
    CHECK_FALSE(infraforge::domain::terrain::terrainErrorCodeFromName("not_a_code").has_value());
}

TEST_CASE("tile codec round-trips and validates provenance") {
    using namespace infraforge::domain::terrain;

    TerrainTileFile tile;
    tile.datasetUuid = "12345678-1234-5678-1234-567812345678";
    tile.datasetRevision = 3;
    tile.chunkX = -2;
    tile.chunkY = 7;
    for (std::uint32_t level = 0; level < kTerrainTileLodCount; ++level) {
        TerrainTileLodGrid grid;
        grid.dim = terrainTileLodDim(level);
        grid.originEasting = 500000.0 + static_cast<double>(level);
        grid.originNorthing = 4650320.0;
        grid.cellEasting = 2.5;
        grid.cellNorthing = 2.5;
        grid.minZ = 100.0;
        grid.maxZ = 200.0;
        grid.heights.assign(static_cast<std::size_t>(grid.dim) * grid.dim, 150.0 + level);
        tile.lods.push_back(std::move(grid));
    }

    const std::string encoded = encodeTerrainTile(tile);
    const TerrainTileFile decoded = decodeTerrainTile(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(encoded.data()), encoded.size()));

    CHECK(decoded.datasetUuid == tile.datasetUuid);
    CHECK(decoded.datasetRevision == tile.datasetRevision);
    CHECK(decoded.chunkX == tile.chunkX);
    CHECK(decoded.chunkY == tile.chunkY);
    REQUIRE(decoded.lods.size() == kTerrainTileLodCount);
    for (std::uint32_t level = 0; level < kTerrainTileLodCount; ++level) {
        CHECK(decoded.lods[level].dim == terrainTileLodDim(level));
        CHECK(decoded.lods[level].heights.size()
            == static_cast<std::size_t>(decoded.lods[level].dim) * decoded.lods[level].dim);
        CHECK(decoded.lods[level].heights.front() == 150.0 + level);
    }

    // Encoding is deterministic (cache files are comparable byte-for-byte).
    CHECK(encodeTerrainTile(decoded) == encoded);

    SUBCASE("wrong magic is rejected") {
        std::string tampered = encoded;
        tampered[0] = 'X';
        CHECK_THROWS_AS((void)decodeTerrainTile(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(tampered.data()), tampered.size())),
            TerrainTileFormatError);
    }
    SUBCASE("truncation is rejected") {
        std::string truncated = encoded.substr(0, encoded.size() - 8);
        CHECK_THROWS_AS((void)decodeTerrainTile(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(truncated.data()), truncated.size())),
            TerrainTileFormatError);
    }
    SUBCASE("unknown schema version is rejected") {
        TerrainTileFile stale = tile;
        stale.schemaVersion = kTerrainTileSchemaVersion + 1;
        std::string staleEncoded = encodeTerrainTile(stale);
        // Patch the stored version field back to the current one so the
        // codec is asked to decode a future schema marker.
        staleEncoded[4] = static_cast<char>(0x7F);
        CHECK_THROWS_AS((void)decodeTerrainTile(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(staleEncoded.data()), staleEncoded.size())),
            TerrainTileFormatError);
    }
}

// BLOCKER 3 regression: an all-NoData tile encodes and decodes correctly.
// The renderer's buildPayload produces zero indices for all-NoData tiles;
// the codec must handle all-NaN heights without error.
TEST_CASE("all-NoData tile round-trips with NaN heights") {
    using namespace infraforge::domain::terrain;

    TerrainTileFile tile;
    tile.datasetUuid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    tile.datasetRevision = 1;
    tile.chunkX = 0;
    tile.chunkY = 0;
    for (std::uint32_t level = 0; level < kTerrainTileLodCount; ++level) {
        TerrainTileLodGrid grid;
        grid.dim = terrainTileLodDim(level);
        grid.originEasting = 500000.0;
        grid.originNorthing = 4650000.0;
        grid.cellEasting = 2.5;
        grid.cellNorthing = 2.5;
        grid.minZ = std::numeric_limits<double>::quiet_NaN();
        grid.maxZ = std::numeric_limits<double>::quiet_NaN();
        // Every cell is NoData (quiet NaN).
        grid.heights.assign(
            static_cast<std::size_t>(grid.dim) * grid.dim,
            std::numeric_limits<double>::quiet_NaN());
        tile.lods.push_back(std::move(grid));
    }

    const std::string encoded = encodeTerrainTile(tile);
    const TerrainTileFile decoded = decodeTerrainTile(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(encoded.data()), encoded.size()));

    CHECK(decoded.datasetUuid == tile.datasetUuid);
    REQUIRE(decoded.lods.size() == kTerrainTileLodCount);
    for (std::uint32_t level = 0; level < kTerrainTileLodCount; ++level) {
        CHECK(decoded.lods[level].heights.size()
            == static_cast<std::size_t>(decoded.lods[level].dim) * decoded.lods[level].dim);
        // Every decoded height must be NaN (NoData preserved).
        for (const double height : decoded.lods[level].heights) {
            CHECK(std::isnan(height));
        }
    }
}

TEST_CASE("TerrainExportEngine exports GeoTIFF, PNG16, and Raw R16 with manifest") {
    using namespace infraforge::application;
    const auto tempDir = std::filesystem::temp_directory_path() / "infraforge_test_terrain_export";
    std::filesystem::create_directories(tempDir);

    const auto sourceTiff = tempDir / "source_dem.tif";
    infraforge::testhelpers::TerrainDemSpec spec;
    spec.width = 64;
    spec.height = 64;
    spec.withNodata = false;
    infraforge::testhelpers::writeDemGeoTiff(sourceTiff, spec);

    SUBCASE("Export GeoTIFF Float32") {
        const auto outDir = tempDir / "export_geotiff";
        TerrainExportOptions options;
        options.datasetUuid = "test-dem-uuid";
        options.sourceRasterPath = sourceTiff;
        options.outputDirectory = outDir;
        options.heightmapFormat = ExportHeightmapFormat::GeoTiffFloat32;
        options.displayName = "TestAlpine";
        options.minElevation = 100.0;
        options.maxElevation = 200.0;

        const auto output = TerrainExportEngine::executeExport(options);
        CHECK(std::filesystem::exists(output.manifestPath));
        CHECK(std::filesystem::file_size(output.manifestPath) > 0);
        REQUIRE(output.exportedFiles.size() == 2);
        CHECK(std::filesystem::exists(output.exportedFiles[0]));
        CHECK(output.exportedFiles[0].extension() == ".tif");
        CHECK(output.exportedFiles[1].filename() == "manifest.json");
        CHECK(output.totalBytes > 0);
    }

    SUBCASE("Export 16-bit PNG heightmap with resolution override") {
        const auto outDir = tempDir / "export_png16";
        TerrainExportOptions options;
        options.datasetUuid = "test-dem-uuid";
        options.sourceRasterPath = sourceTiff;
        options.outputDirectory = outDir;
        options.heightmapFormat = ExportHeightmapFormat::Png16;
        options.targetResolution = 128;
        options.displayName = "TestAlpinePng";
        options.minElevation = 100.0;
        options.maxElevation = 200.0;

        const auto output = TerrainExportEngine::executeExport(options);
        CHECK(std::filesystem::exists(output.manifestPath));
        REQUIRE(output.exportedFiles.size() == 2);
        CHECK(std::filesystem::exists(output.exportedFiles[0]));
        CHECK(output.exportedFiles[0].extension() == ".png");
        CHECK(output.exportedFiles[1].filename() == "manifest.json");
    }

    SUBCASE("Export Raw R16 heightmap") {
        const auto outDir = tempDir / "export_raw16";
        TerrainExportOptions options;
        options.datasetUuid = "test-dem-uuid";
        options.sourceRasterPath = sourceTiff;
        options.outputDirectory = outDir;
        options.heightmapFormat = ExportHeightmapFormat::RawR16;
        options.targetResolution = 64;
        options.displayName = "TestAlpineRaw";
        options.minElevation = 100.0;
        options.maxElevation = 200.0;

        const auto output = TerrainExportEngine::executeExport(options);
        CHECK(std::filesystem::exists(output.manifestPath));
        REQUIRE(output.exportedFiles.size() == 2);
        CHECK(std::filesystem::exists(output.exportedFiles[0]));
        CHECK(output.exportedFiles[0].extension() == ".r16");
        CHECK(output.exportedFiles[1].filename() == "manifest.json");
        // 64 * 64 * 2 bytes = 8192 bytes
        CHECK(std::filesystem::file_size(output.exportedFiles[0]) == 64 * 64 * 2);
    }

    std::filesystem::remove_all(tempDir);
}

} // TEST_SUITE

