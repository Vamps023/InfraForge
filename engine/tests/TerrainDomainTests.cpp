#include <doctest/doctest.h>

#include "infraforge/domain/terrain/TerrainDataset.hpp"
#include "infraforge/domain/terrain/TerrainTileFile.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"

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

} // TEST_SUITE
