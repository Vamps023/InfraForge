#include <doctest/doctest.h>

#include "infraforge/application/JobSystem.hpp"
#include "infraforge/application/TerrainService.hpp"
#include "infraforge/application/TerrainTileGenerator.hpp"
#include "infraforge/domain/terrain/TerrainTileFile.hpp"
#include "infraforge/application/WorldState.hpp"
#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/persistence/GdalTerrainSource.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"

#include "TerrainTestFixtures.hpp"
#include "TestHelpers.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>

using namespace infraforge::application;
using namespace infraforge::domain::terrain;

namespace {

// Deterministic executor stand-in for the real CommandProcessor executor.
class QueuedExecutor {
public:
    JobSystem::ExecutorPoster poster() {
        return [this](std::function<void()> task) {
            std::lock_guard lock{mutex_};
            tasks_.push_back(std::move(task));
        };
    }

    void drain() {
        std::deque<std::function<void()>> local;
        {
            std::lock_guard lock{mutex_};
            local.swap(tasks_);
        }
        while (!local.empty()) {
            local.front()();
            local.pop_front();
        }
    }

private:
    std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
};

// A real project + services stack: the import path under test is the
// production one (GDAL reader, PROJ transforms, SQLite store, worker jobs).
struct TerrainHarness {
    infraforge::testhelpers::ScratchDirectory scratch;
    QueuedExecutor executor;
    infraforge::persistence::SqliteProjectStore store;
    infraforge::domain::geo::GeoTransformService transforms;
    infraforge::persistence::GdalTerrainSource reader;
    infraforge::application::WorldState world;
    std::optional<infraforge::application::JobSystem> jobs;
    std::optional<infraforge::application::TerrainService> terrain;
    std::filesystem::path demPath;
    std::filesystem::path projectDirectory;
    std::vector<std::string> datasetAddedIds;

    explicit TerrainHarness(const infraforge::testhelpers::TerrainDemSpec& spec = {})
        : jobs([this](std::function<void()> task) { executor.poster()(std::move(task)); }),
          terrain(std::in_place, store, transforms, reader, world, *jobs,
              [this](const infraforge::application::TerrainServiceEvent& event) {
                  if (event.datasetAdded.has_value()) {
                      datasetAddedIds.push_back(
                          infraforge::domain::terrain::uuidTextFromEntityId(event.datasetAdded->id));
                  }
              }) {
        demPath = scratch.path() / "source-dem.tif";
        infraforge::testhelpers::writeDemGeoTiff(demPath, spec);

        infraforge::domain::project::CreateProjectSpec createSpec;
        createSpec.displayName = "Terrain Tests";
        createSpec.parentDirectory = scratch.path();
        createSpec.trafficSide = infraforge::domain::project::TrafficSide::Right;
        createSpec.georeference.horizontalCrs = "EPSG:32633";
        createSpec.georeference.linearUnit = "metre";
        createSpec.georeference.axisConvention = infraforge::domain::geo::AxisConvention::EastingNorthingUp;
        createSpec.georeference.originEasting = 500000.0;
        createSpec.georeference.originNorthing = 4650000.0;
        (void)store.create(createSpec);
        projectDirectory = std::filesystem::path(store.current().directory);
        terrain->onProjectOpened();
    }

    template <typename Pred>
    bool waitFor(const Pred& predicate, std::chrono::milliseconds budget) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline) {
            executor.drain();
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        executor.drain();
        return predicate();
    }
};

[[nodiscard]] bool isTerminal(const std::optional<infraforge::application::JobRecord>& job) {
    return job.has_value()
        && (job->state == infraforge::application::JobState::Completed
            || job->state == infraforge::application::JobState::Failed
            || job->state == infraforge::application::JobState::Cancelled);
}

} // namespace

TEST_SUITE("terrain import and sampling") {

TEST_CASE("probe reports detected CRS, geometry, unit, and NoData") {
    infraforge::testhelpers::ScratchDirectory scratch;
    const auto demPath = scratch.path() / "probe-dem.tif";
    infraforge::testhelpers::writeDemGeoTiff(demPath, {});

    infraforge::persistence::GdalTerrainSource reader;
    const auto info = reader.probe(demPath);
    CHECK(info.format == "GTiff");
    const bool crsIdentifiable = info.crsDefinition.find("32633") != std::string::npos
        || info.crsDefinition.find("UTM") != std::string::npos;
    CHECK(crsIdentifiable);
    CHECK(info.width == 32);
    CHECK(info.height == 32);
    CHECK(info.pixelSizeX == doctest::Approx(10.0));
    CHECK(info.pixelSizeY == doctest::Approx(10.0));
    CHECK(info.elevationUnit == "metre");
    CHECK(info.hasNodata);
    CHECK(info.nodataValue == doctest::Approx(-9999.0));
}

TEST_CASE("probe rejects CRS-less rasters explicitly") {
    infraforge::testhelpers::ScratchDirectory scratch;
    const auto demPath = scratch.path() / "no-crs.tif";
    GDALAllRegister();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    REQUIRE(driver != nullptr);
    GDALDataset* dataset = driver->Create(demPath.string().c_str(), 8, 8, 1, GDT_Float32, nullptr);
    REQUIRE(dataset != nullptr);
    // Deliberately no geotransform and no projection: nothing to assume.
    GDALClose(dataset);

    infraforge::persistence::GdalTerrainSource reader;
    bool threw = false;
    try {
        (void)reader.probe(demPath);
    } catch (const infraforge::domain::terrain::TerrainError& error) {
        threw = true;
        CHECK(error.code() == infraforge::domain::terrain::TerrainErrorCode::MissingCrs);
    }
    CHECK(threw);
}

TEST_CASE("truncated sources are detected as corrupt, not imported") {
    infraforge::testhelpers::ScratchDirectory scratch;
    const auto full = scratch.path() / "full.tif";
    infraforge::testhelpers::writeDemGeoTiff(full, {});
    const auto truncated = scratch.path() / "truncated.tif";
    {
        std::ifstream input(full, std::ios::binary);
        REQUIRE(input.good());
        std::string bytes(200, '\0');
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));
        std::ofstream output(truncated, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(output.good());
    }

    infraforge::persistence::GdalTerrainSource reader;
    bool threw = false;
    try {
        (void)reader.probe(truncated);
    } catch (const infraforge::domain::terrain::TerrainError& error) {
        threw = true;
        CHECK(error.code() == infraforge::domain::terrain::TerrainErrorCode::CorruptSource);
    }
    CHECK(threw);
}

TEST_CASE("import produces canonical dataset, sampling, tiles, and survives reopen") {
    TerrainHarness harness{};

    const auto record = harness.terrain->startImport(
        {.sourcePath = harness.demPath, .displayName = "Area DEM"});
    const bool finished = harness.waitFor(
        [&] { return isTerminal(harness.jobs->job(record.jobId)); },
        std::chrono::seconds{30});
    REQUIRE(finished);
    const auto job = harness.jobs->job(record.jobId);
    REQUIRE(job->state == infraforge::application::JobState::Completed);

    // Canonical dataset committed with correct identity and coverage.
    // terrainDatasets() returns by value; keep the vector alive for .
    const auto storedDatasets = harness.store.terrainDatasets();
    REQUIRE(storedDatasets.size() == 1);
    const auto& dataset = storedDatasets.front();
    CHECK(dataset.displayName == "Area DEM");
    const bool crsResolved = dataset.sourceCrs.find("32633") != std::string::npos;
    CHECK(crsResolved);
    CHECK(dataset.bounds.maxEasting == doctest::Approx(500320.0).epsilon(1e-6));
    CHECK(dataset.bounds.minEasting == doctest::Approx(500000.0).epsilon(1e-6));
    CHECK(dataset.bounds.maxNorthing == doctest::Approx(4650320.0).epsilon(1e-6));
    CHECK(dataset.bounds.minNorthing == doctest::Approx(4650000.0).epsilon(1e-6));
    CHECK(dataset.minZ == doctest::Approx(100.0).epsilon(1e-6));
    CHECK(dataset.maxZ == doctest::Approx(123.25).epsilon(1e-6));
    CHECK(dataset.sourceBytes > 0);
    CHECK(dataset.sourceSha256.size() == 64);
    CHECK(dataset.elevationUnit == "metre");
    // Project revision advanced (canonical mutation) and the session is dirty.
    CHECK(harness.store.current().revision == 2);
    CHECK(harness.store.current().isDirty());
    CHECK(harness.datasetAddedIds.size() == 1);

    // The dataset is registered in the world partition with terrain-class
    // invalidation: exactly the chunks its canonical bounds intersect.
    CHECK(harness.world.boundsOf(dataset.id).has_value());
    const auto chunks = harness.world.chunksIntersecting(dataset.bounds);
    CHECK(chunks.size() == 1);
    CHECK(harness.world.lastAffectingRevision(
        chunks.front(), infraforge::domain::world::InvalidationMask::of(
            infraforge::domain::world::InvalidationClass::Terrain)) > 0);

    // Canonical sampling through project-global coordinates (source and
    // project CRS agree here, so values pass through unchanged).
    SUBCASE("exact cell center reproduces the stored sample") {
        // Cell (row 2, col 3) center: (500035, 4650295) -> 100 + 1.5 + 0.5.
        const auto sample = harness.terrain->sample("", 500035.0, 4650295.0);
        CHECK(sample.sample.status == infraforge::domain::terrain::TerrainSampleStatus::Height);
        CHECK(sample.sample.height == doctest::Approx(102.0).epsilon(1e-9));
        CHECK(sample.datasetUuid == infraforge::domain::terrain::uuidTextFromEntityId(dataset.id));
    }
    SUBCASE("bilinear interpolation between cell centers") {
        // Midpoint of the four cells around (500040, 4650290).
        const auto sample = harness.terrain->sample("", 500040.0, 4650290.0);
        CHECK(sample.sample.status == infraforge::domain::terrain::TerrainSampleStatus::Height);
        CHECK(sample.sample.height == doctest::Approx(102.375).epsilon(1e-9));
    }
    SUBCASE("NoData cells report NoData, never a substituted height") {
        const auto sample = harness.terrain->sample("", 500055.0, 4650265.0);
        CHECK(sample.sample.status == infraforge::domain::terrain::TerrainSampleStatus::NoData);
    }
    SUBCASE("outside coverage reports OutsideCoverage") {
        const auto sample = harness.terrain->sample("", 499000.0, 4650000.0);
        CHECK(sample.sample.status == infraforge::domain::terrain::TerrainSampleStatus::OutsideCoverage);
    }

    // Derived tiles: the tiles job starts when the import job commits;
    // wait for it to finish before asserting cache presence.
    const auto datasetUuidText = infraforge::domain::terrain::uuidTextFromEntityId(dataset.id);
    const bool tilesSettled = harness.waitFor(
        [&] {
            const auto snapshot = harness.terrain->datasetDetails(datasetUuidText);
            return snapshot.presentTiles == snapshot.expectedTiles;
        },
        std::chrono::seconds{30});
    REQUIRE(tilesSettled);
    infraforge::persistence::GdalTerrainSource reader;
    const auto details = harness.terrain->datasetDetails(datasetUuidText);
    CHECK(details.expectedTiles == 1);
    CHECK(details.presentTiles == 1);
    const auto tilePath = infraforge::application::TerrainTileGenerator::tilePath(
        harness.projectDirectory,
        infraforge::domain::terrain::uuidTextFromEntityId(dataset.id), chunks.front().x, chunks.front().y);
    REQUIRE(std::filesystem::is_regular_file(tilePath));
    const auto decodedTile = infraforge::domain::terrain::decodeTerrainTileFromFile(tilePath.string());
    CHECK(decodedTile.datasetUuid == infraforge::domain::terrain::uuidTextFromEntityId(dataset.id));
    CHECK(decodedTile.datasetRevision == 1);
    CHECK(decodedTile.lods.front().dim == 129);
    // NoData hole at cell (5,5): the tile's level-0 grid covers it with NaN.
    bool foundNodata = false;
    for (const double height : decodedTile.lods.front().heights) {
        if (std::isnan(height)) {
            foundNodata = true;
            break;
        }
    }
    CHECK(foundNodata);
    // Tile heights agree with canonical sampling at the same positions.
    const auto& grid = decodedTile.lods.front();
    const auto mid = (grid.dim / 2);
    const double midE = grid.originEasting + static_cast<double>(mid) * grid.cellEasting;
    const double midN = grid.originNorthing - static_cast<double>(mid) * grid.cellNorthing;
    const auto reference = harness.terrain->sample("", midE, midN);
    REQUIRE(reference.sample.status == infraforge::domain::terrain::TerrainSampleStatus::Height);
    CHECK(grid.heights[static_cast<std::size_t>(mid) * grid.dim + mid]
        == doctest::Approx(reference.sample.height).epsilon(1e-9));

    // Reopen: canonical state, world registration, and sampling are restored.
    harness.store.close();
    harness.terrain->onProjectClosed();
    (void)harness.store.open(harness.projectDirectory);
    harness.terrain->onProjectOpened();
    REQUIRE(harness.terrain->listDatasets().size() == 1);
    const auto reopened = harness.terrain->listDatasets().front();
    CHECK(reopened.id == dataset.id);
    const auto afterReopen = harness.terrain->sample("", 500035.0, 4650295.0);
    CHECK(afterReopen.sample.status == infraforge::domain::terrain::TerrainSampleStatus::Height);
    CHECK(afterReopen.sample.height == doctest::Approx(102.0).epsilon(1e-9));
    const auto detailsAfter = harness.terrain->datasetDetails(
        infraforge::domain::terrain::uuidTextFromEntityId(reopened.id));
    CHECK(detailsAfter.presentTiles == 1);
}

TEST_CASE("cancelling a queued import commits nothing; the running import completes") {
    // A 300x200 DEM at 10 m cells covers ~3 km x 2 km = 6 chunks (bounded
    // tile generation). Kept small so the test stays fast; the cancellation
    // semantics are independent of raster size.
    TerrainHarness harness{infraforge::testhelpers::TerrainDemSpec{
        .width = 300, .height = 200, .originX = 500000.0, .originY = 4652000.0,
        .cellSize = 10.0, .nodataRow = 5, .nodataCol = 5, .withNodata = false}};

    // Occupies the serial worker.
    const auto first = harness.terrain->startImport(
        {.sourcePath = harness.demPath, .displayName = "Big DEM"});
    // Waits behind it; cancelled before it ever runs.
    infraforge::testhelpers::writeDemGeoTiff(harness.scratch.path() / "small.tif", {});
    const auto second = harness.terrain->startImport(
        {.sourcePath = harness.scratch.path() / "small.tif", .displayName = "Small DEM"});

    const bool blockerRunning = harness.waitFor(
        [&] {
            const auto snapshot = harness.jobs->job(first.jobId);
            return snapshot.has_value() && snapshot->state == infraforge::application::JobState::Running;
        },
        std::chrono::seconds{30});
    REQUIRE(blockerRunning);
    REQUIRE(harness.jobs->requestCancel(second.jobId));

    const bool bothSettled = harness.waitFor(
        [&] {
            return isTerminal(harness.jobs->job(first.jobId))
                && isTerminal(harness.jobs->job(second.jobId));
        },
        std::chrono::seconds{60});
    REQUIRE(bothSettled);

    CHECK(harness.jobs->job(second.jobId)->state == infraforge::application::JobState::Cancelled);
    CHECK(harness.jobs->job(first.jobId)->state == infraforge::application::JobState::Completed);
    // Only the running import committed; the cancelled one left nothing.
    REQUIRE(harness.store.terrainDatasets().size() == 1);
    CHECK(harness.store.terrainDatasets().front().displayName == "Big DEM");
    CHECK(harness.datasetAddedIds.size() == 1);
    // No .importing temp files survive either outcome.
    bool tempLeft = false;
    for (const auto& entry :
        std::filesystem::directory_iterator(harness.projectDirectory / "terrain" / "elevation")) {
        if (entry.path().extension() == ".importing") {
            tempLeft = true;
        }
    }
    CHECK_FALSE(tempLeft);
}

} // TEST_SUITE
