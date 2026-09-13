#pragma once

#include "infraforge/application/JobSystem.hpp"
#include "infraforge/application/WorldState.hpp"
#include "infraforge/domain/geo/ProjectGeoreference.hpp"
#include "infraforge/domain/terrain/TerrainDataset.hpp"
#include "infraforge/domain/terrain/TerrainDownloadProvider.hpp"
#include "infraforge/domain/terrain/TerrainSampler.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/domain/world/ChunkGrid.hpp"
#include "infraforge/domain/world/SpatialIndex.hpp"
#include "infraforge/domain/world/SpatialBounds.hpp"
#include "infraforge/ports/ProjectStore.hpp"
#include "infraforge/ports/TerrainSource.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace infraforge::application {

// Events produced by terrain use cases, marshalled onto the application
// executor and translated into protocol events by the transport layer.
struct TerrainServiceEvent {
    // Job lifecycle updates (all five job states).
    std::optional<JobRecord> job;
    // Canonical dataset addition (after the store transaction committed).
    std::optional<domain::terrain::TerrainDataset> datasetAdded;
    // Project revision snapshot carried by canonical mutation events.
    std::uint64_t revision{0};
};

// Detected facts about a candidate DEM source (probe is read-only and
// precedes any project mutation).
struct TerrainProbeResult {
    ports::TerrainSourceInfo source;
    // Geo-resolved CRS description for display; empty when the source has
    // no CRS (which fails the probe with TerrainError(MissingCrs), so a
    // present description always corresponds to a resolvable CRS).
    std::string crsName;
    std::string crsKind;
    std::string crsAuthority;
    std::string crsCode;
};

struct TerrainImportSpec {
    // Absolute path of the external DEM source file.
    std::filesystem::path sourcePath;
    std::string displayName;
};

// Inspector projection of one dataset: canonical record plus derived tile
// state (existence of cache files), never the raster payload itself.
struct TerrainDatasetDetails {
    domain::terrain::TerrainDataset dataset;
    std::uint64_t expectedTiles{0};
    std::uint64_t presentTiles{0};
};

// Renderer-facing scene projection: derived terrain tiles with session
// context paths. Consumed by the viewport process through the desktop
// shell; the frontend never interprets raster payloads.
struct TerrainSceneTile {
    std::string datasetUuid;
    std::uint64_t datasetRevision{0};
    std::int64_t chunkX{0};
    std::int64_t chunkY{0};
    // Session-context absolute path of the derived tile cache file.
    std::string absolutePath;
    // Canonical coverage of the tile (project-global, closed edges).
    double minEasting{0.0};
    double minNorthing{0.0};
    double maxEasting{0.0};
    double maxNorthing{0.0};
};

struct TerrainSceneProjection {
    domain::geo::ProjectGlobalPosition renderOrigin;
    std::vector<TerrainSceneTile> tiles;
    std::uint64_t missingTiles{0};
    std::uint64_t revision{0};
};

struct TerrainSampleResult {
    std::string datasetUuid; // dataset that resolved the sample
    domain::terrain::TerrainSample sample;
};

// Application boundary of the Terrain domain: import job orchestration,
// canonical sampling queries, dataset projections, and renderer scene
// projection. Runs on the single application executor; heavy raster work
// is delegated to JobSystem workers with immutable inputs, and worker
// outcomes are applied transactionally here.
class TerrainService {
public:
    // `eventSink` is invoked on the executor; the transport layer
    // translates the events into protocol frames.
    using EventSink = std::function<void(const TerrainServiceEvent&)>;

    TerrainService(ports::ProjectStore& store, const domain::geo::GeoTransformService& transforms,
        ports::TerrainSourceReader& reader, WorldState& world, JobSystem& jobs,
        EventSink eventSink);

    // Rebuilds session state after project create/open (georeference
    // resolution, world partition, dataset registry replay).
    void onProjectOpened();
    void onProjectClosed();

    // Read-only source detection (CRS present/missing, raster geometry).
    // Throws TerrainError/GeoError with actionable diagnostics.
    [[nodiscard]] TerrainProbeResult probeSource(const std::filesystem::path& file) const;

    // Starts the import job: copy into project-owned storage with
    // progress, validate, transform coverage, commit canonically, then
    // generate derived tiles. Returns the queued job record.
    [[nodiscard]] JobRecord startImport(const TerrainImportSpec& spec);

    // Re-generates missing/stale derived tiles for one dataset.
    [[nodiscard]] JobRecord regenerateTiles(const std::string& datasetUuid);

    // ---- Download Area workflow (Issue #6 BLOCKER 7) ----

    // List available terrain DEM providers.
    [[nodiscard]] std::vector<domain::terrain::ProviderInfo> listSources() const;

    // Compute a deterministic download plan for the selected tiles. Does NOT
    // perform any network I/O.
    [[nodiscard]] domain::terrain::DownloadPlan planDownload(
        const std::string& providerId,
        const domain::terrain::GeoBounds& area,
        std::uint32_t tileSizeMetres,
        const std::vector<std::int32_t>& selectedIndices) const;

    // Start a background download job for the selected tiles. The job
    // acquires provider data, decodes, assembles canonical coverage, and
    // commits through the same canonical ingestion path as local-file import.
    [[nodiscard]] JobRecord startDownload(
        const std::string& providerId,
        const domain::terrain::GeoBounds& area,
        std::uint32_t tileSizeMetres,
        const std::vector<std::int32_t>& selectedIndices,
        const std::string& displayName);

    [[nodiscard]] std::vector<domain::terrain::TerrainDataset> listDatasets() const;
    [[nodiscard]] TerrainDatasetDetails datasetDetails(const std::string& datasetUuid) const;
    [[nodiscard]] TerrainSceneProjection sceneProjection() const;

    // Canonical double-precision sampling in project-global coordinates.
    // Without a dataset id the most recently imported covering dataset
    // resolves the sample (deterministic, documented semantics).
    [[nodiscard]] TerrainSampleResult sample(
        const std::string& datasetUuid, double easting, double northing) const;

    // Whether any canonical terrain datasets exist (used to guard
    // georeference changes, which would invalidate derived coverage).
    [[nodiscard]] bool hasDatasets() const noexcept { return !datasets_.empty(); }

private:
    struct ImportPayload {
        domain::terrain::TerrainDataset dataset;   // fully populated candidate
        std::string projectUuid;                   // identity guard for late completion
        std::filesystem::path projectDirectory;
        std::filesystem::path tempFile;            // project-owned .importing temp
        domain::world::ChunkGrid grid;             // tile-scope capture
        domain::geo::ProjectGeoreference project;  // immutable value snapshot
    };
    struct TilesPayload {
        std::string datasetUuid;
        std::uint64_t generated{0};
        std::uint64_t skipped{0};
    };
    // Immutable inputs for the download worker (Issue #6 BLOCKER 7).
    struct DownloadPayload {
        std::string jobId;
        std::string providerId;
        std::vector<domain::terrain::SelectionTile> selectedTiles;
        std::vector<domain::terrain::ProviderRequest> requests;
        std::string displayName;
        std::string projectUuid;
        std::filesystem::path projectDirectory;
        domain::geo::ProjectGeoreference project;
    };

    [[nodiscard]] std::filesystem::path projectDirectory_() const;
    [[nodiscard]] std::optional<domain::terrain::TerrainDataset> findDataset(
        const std::string& datasetUuid) const;
    [[nodiscard]] std::vector<domain::world::ChunkCoord> expectedChunks(
        const domain::terrain::TerrainDataset& dataset) const;
    void emitJobUpdate(const JobRecord& record);
    void commitImportedDataset(ImportPayload payload);
    void submitTileGenerationJob(ImportPayload payload);
    void trackJob(const std::string& jobId);
    void untrackJob(const std::string& jobId);
    void cancelTrackedJobs();

    ports::ProjectStore& store_;
    const domain::geo::GeoTransformService& transforms_;
    ports::TerrainSourceReader& reader_;
    WorldState& world_;
    JobSystem& jobs_;
    EventSink eventSink_;
    domain::terrain::TerrainProviderRegistry providers_;

    std::optional<domain::geo::ProjectGeoreference> project_;
    std::vector<domain::terrain::TerrainDataset> datasets_; // creation order
    std::vector<std::string> activeJobIds_;                 // cancellable on close
    std::uint64_t revision_{0};
};

} // namespace infraforge::application
