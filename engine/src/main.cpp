#include "infraforge/application/CommandProcessor.hpp"
#include "infraforge/application/JobSystem.hpp"
#include "infraforge/application/TerrainService.hpp"
#include "infraforge/application/WorldState.hpp"
#include "infraforge/domain/geo/GeoTransformService.hpp"
#include "infraforge/domain/terrain/TerrainTileFile.hpp"
#include "infraforge/domain/terrain/TerrainTypes.hpp"
#include "infraforge/network/CommandRouter.hpp"
#include "infraforge/network/WebSocketServer.hpp"
#include "infraforge/persistence/GdalTerrainSource.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"
#include "infraforge/runtime/Logging.hpp"
#include "infraforge/version.hpp"
#include "infraforge/protocol/v1/foundation.pb.h"

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cstdlib>

#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

// Executable directory for deployed-layout probes (share/proj).
std::filesystem::path argv0_directory() {
    std::error_code error;
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path{buffer}.parent_path();
    }
#else
    std::error_code readError;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", readError);
    if (!readError) {
        return self.parent_path();
    }
#endif
    return std::filesystem::current_path(error);
}

struct ServeArguments {
    std::string host;
    std::uint16_t port{};
    std::string session_token;
};

void print_usage() {
    std::cout
        << "InfraForge native engine\n\n"
        << "Usage:\n"
        << "  infraforge-engine --version\n"
        << "  infraforge-engine --self-check\n"
        << "  infraforge-engine --serve --host 127.0.0.1 --port <port> --session-token <64-hex-token>\n"
        << "  infraforge-engine --help\n";
}

bool is_hex_token(std::string_view token) {
    if (token.size() != 64) {
        return false;
    }
    for (const char value : token) {
        if (std::isxdigit(static_cast<unsigned char>(value)) == 0) {
            return false;
        }
    }
    return true;
}

std::optional<std::uint16_t> parse_port(std::string_view text) {
    unsigned int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed == 0 || parsed > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::optional<ServeArguments> parse_serve_arguments(int argc, char** argv) {
    ServeArguments arguments;
    bool have_host = false;
    bool have_port = false;
    bool have_token = false;

    for (int index = 2; index < argc; index += 2) {
        if (index + 1 >= argc) {
            return std::nullopt;
        }

        const std::string_view key{argv[index]};
        const std::string_view value{argv[index + 1]};

        if (key == "--host" && !have_host) {
            arguments.host = value;
            have_host = true;
        } else if (key == "--port" && !have_port) {
            const auto port = parse_port(value);
            if (!port.has_value()) {
                return std::nullopt;
            }
            arguments.port = *port;
            have_port = true;
        } else if (key == "--session-token" && !have_token) {
            arguments.session_token = value;
            have_token = true;
        } else {
            return std::nullopt;
        }
    }

    if (!have_host || !have_port || !have_token) {
        return std::nullopt;
    }

    if (arguments.host != "127.0.0.1" || !is_hex_token(arguments.session_token)) {
        return std::nullopt;
    }

    return arguments;
}

// 4. Terrain vertical self-check: a real GDAL-written GeoTIFF travels the
//    production path — import job (copy/validate/commit), canonical
//    sampling, derived tile generation — against a real temporary project.
int run_terrain_self_check(const std::filesystem::path& parentDirectory) {
    namespace fs = std::filesystem;
    using namespace infraforge;

    std::error_code makeError;
    fs::create_directories(parentDirectory, makeError);
    if (makeError) {
        std::cerr << "Terrain self-check failed: cannot create scratch directory\n";
        return 1;
    }

    // GDAL resolves CRS definitions through its own PROJ context, which the
    // Geo service does not configure (it scopes search paths to its private
    // PJ_CONTEXT). Point GDAL at the same data directory before any raster
    // operation: an existing PROJ_DATA/PROJ_LIB wins, then the compiled-in
    // vcpkg share path, then the exe-relative deployed layout.
    {
        // MSVC deprecates getenv; these are one-time startup reads.
#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* existing = std::getenv("PROJ_DATA");
        if (existing == nullptr || *existing == '\0') {
            existing = std::getenv("PROJ_LIB");
        }
#ifdef _WIN32
#pragma warning(pop)
#endif
        if (existing == nullptr || *existing == '\0') {
            fs::path projData;
#ifdef INFRAFORGE_PROJ_DATA_DIR
            if (fs::exists(fs::path{INFRAFORGE_PROJ_DATA_DIR} / "proj.db")) {
                projData = INFRAFORGE_PROJ_DATA_DIR;
            }
#endif
            if (projData.empty()) {
                std::error_code probeError;
                const fs::path deployed =
                    fs::path{argv0_directory()} / "share" / "proj";
                if (fs::exists(deployed / "proj.db", probeError)) {
                    projData = deployed;
                }
            }
            if (!projData.empty()) {
#ifdef _WIN32
                (void)_putenv_s("PROJ_DATA", projData.string().c_str());
#else
                (void)setenv("PROJ_DATA", projData.string().c_str(), 0);
#endif
            }
        }
    }

    // Deterministic 32x32 UTM 33N DEM: z(r,c) = 100 + 0.5c + 0.25r, one
    // NoData cell, 10 m pixels anchored at (500000, 4650320).
    const fs::path demPath = parentDirectory / "selfcheck-dem.tif";
    {
        GDALAllRegister();
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (driver == nullptr) {
            std::cerr << "Terrain self-check failed: GTiff driver unavailable\n";
            return 1;
        }
        GDALDataset* dataset =
            driver->Create(demPath.string().c_str(), 32, 32, 1, GDT_Float32, nullptr);
        if (dataset == nullptr) {
            std::cerr << "Terrain self-check failed: cannot create DEM\n";
            return 1;
        }
        const double geotransform[6] = {500000.0, 10.0, 0.0, 4650320.0, 0.0, -10.0};
        dataset->SetGeoTransform(const_cast<double*>(geotransform));
        OGRSpatialReference srs;
        srs.SetFromUserInput("EPSG:32633");
        char* wkt = nullptr;
        srs.exportToWkt(&wkt);
        dataset->SetProjection(wkt);
        CPLFree(wkt);
        GDALRasterBand* band = dataset->GetRasterBand(1);
        band->SetNoDataValue(-9999.0);
        std::vector<float> row(32, 0.0F);
        for (int r = 0; r < 32; ++r) {
            for (int c = 0; c < 32; ++c) {
                row[static_cast<std::size_t>(c)] =
                    (r == 5 && c == 5) ? -9999.0F : static_cast<float>(100.0 + 0.5 * c + 0.25 * r);
            }
            if (band->RasterIO(GF_Write, 0, r, 32, 1, row.data(), 32, 1, GDT_Float32, 0, 0, nullptr)
                != CE_None) {
                GDALClose(dataset);
                std::cerr << "Terrain self-check failed: DEM write error\n";
                return 1;
            }
        }
        GDALClose(dataset);
    }

    // Miniature executor: completion/progress tasks queue and drain on this
    // thread, mirroring the single-application-executor ownership rule.
    std::mutex taskMutex;
    std::deque<std::function<void()>> tasks;
    const auto drainTasks = [&] {
        std::deque<std::function<void()>> local;
        {
            std::lock_guard lock{taskMutex};
            local.swap(tasks);
        }
        while (!local.empty()) {
            local.front()();
            local.pop_front();
        }
    };

    try {
        persistence::SqliteProjectStore store;
        domain::project::CreateProjectSpec spec;
        spec.displayName = "Terrain Self Check";
        spec.parentDirectory = parentDirectory;
        spec.trafficSide = domain::project::TrafficSide::Right;
        spec.georeference.horizontalCrs = "EPSG:32633";
        spec.georeference.linearUnit = "metre";
        spec.georeference.axisConvention = domain::geo::AxisConvention::EastingNorthingUp;
        spec.georeference.originEasting = 500000.0;
        spec.georeference.originNorthing = 4650000.0;
        const auto created = store.create(spec);
        const fs::path projectDirectory{created.directory};

        domain::geo::GeoTransformService transforms;
        persistence::GdalTerrainSource reader;
        application::WorldState world;
        application::JobSystem jobs{[&](std::function<void()> task) {
            std::lock_guard lock{taskMutex};
            tasks.push_back(std::move(task));
        }};
        application::TerrainService terrain{
            store, transforms, reader, world, jobs, [](const application::TerrainServiceEvent&) {}};
        terrain.onProjectOpened();

        const auto importJob = terrain.startImport({.sourcePath = demPath, .displayName = "Self Check DEM"});
        const auto terminal = [&](const application::JobState state) {
            return state == application::JobState::Completed || state == application::JobState::Failed
                || state == application::JobState::Cancelled;
        };
        bool settled = false;
        for (int attempt = 0; attempt < 3000; ++attempt) {
            drainTasks();
            const auto snapshot = jobs.job(importJob.jobId);
            if (snapshot.has_value() && terminal(snapshot->state)) {
                settled = snapshot->state == application::JobState::Completed;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        if (!settled) {
            std::cerr << "Terrain self-check failed: import job did not complete\n";
            return 1;
        }

        const auto datasets = terrain.listDatasets();
        if (datasets.size() != 1) {
            std::cerr << "Terrain self-check failed: canonical dataset missing after import\n";
            return 1;
        }

        // Canonical sampling control point: cell (row 2, col 3) center at
        // (500035, 4650295) must reproduce z = 100 + 1.5 + 0.5 = 102.
        const auto sample = terrain.sample("", 500035.0, 4650295.0);
        if (sample.sample.status != domain::terrain::TerrainSampleStatus::Height
            || std::abs(sample.sample.height - 102.0) > 1e-6) {
            std::cerr << "Terrain self-check failed: sampling control point mismatch\n";
            return 1;
        }

        // Derived tiles settle after the import job; wait for cache presence.
        const auto uuidText = domain::terrain::uuidTextFromEntityId(datasets.front().id);
        bool tilesPresent = false;
        for (int attempt = 0; attempt < 3000; ++attempt) {
            drainTasks();
            const auto details = terrain.datasetDetails(uuidText);
            if (details.presentTiles == details.expectedTiles && details.expectedTiles > 0) {
                tilesPresent = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        if (!tilesPresent) {
            std::cerr << "Terrain self-check failed: derived tiles missing\n";
            return 1;
        }

        // Persistence across reopen: canonical record and sampling survive.
        store.close();
        terrain.onProjectClosed();
        (void)store.open(projectDirectory);
        terrain.onProjectOpened();
        const auto reopened = terrain.listDatasets();
        const auto resampled = terrain.sample("", 500035.0, 4650295.0);
        if (reopened.size() != 1 || reopened.front().id != datasets.front().id
            || resampled.sample.status != domain::terrain::TerrainSampleStatus::Height
            || std::abs(resampled.sample.height - 102.0) > 1e-6) {
            std::cerr << "Terrain self-check failed: reopen round-trip mismatch\n";
            return 1;
        }
        store.close();
    } catch (const std::exception& error) {
        std::cerr << "Terrain self-check failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

int run_self_check() {
    // 1. Protocol frame round-trip including the project lifecycle envelope.
    infraforge::protocol::v1::Frame source;
    source.set_request_id("self-check");
    auto* command = source.mutable_command();
    auto* create = command->mutable_create_project();
    create->set_display_name("Self Check");
    create->set_parent_directory("/tmp/nonexistent");
    auto* georeference = create->mutable_georeference();
    georeference->set_horizontal_crs("EPSG:32633");
    georeference->set_linear_unit("metre");
    georeference->set_axis_convention(infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
    create->set_traffic_side(infraforge::protocol::v1::TRAFFIC_SIDE_RIGHT);

    std::string encoded;
    if (!source.SerializeToString(&encoded)) {
        std::cerr << "Protocol serialization self-check failed\n";
        return 1;
    }

    infraforge::protocol::v1::Frame decoded;
    if (!decoded.ParseFromString(encoded) || !decoded.has_command()
        || !decoded.command().has_create_project()
        || decoded.command().create_project().display_name() != "Self Check"
        || decoded.command().create_project().georeference().horizontal_crs() != "EPSG:32633") {
        std::cerr << "Protocol parse self-check failed\n";
        return 1;
    }

    // 2. Project persistence round-trip against a real temporary directory.
    std::error_code ioError;
    const auto scratchRoot = std::filesystem::temp_directory_path(ioError) / "infraforge-engine-self-check";
    if (ioError) {
        std::cerr << "Cannot resolve temporary directory for self-check\n";
        return 1;
    }
    std::filesystem::remove_all(scratchRoot, ioError);
    const auto parentDirectory = scratchRoot / "projects";
    std::filesystem::create_directories(parentDirectory, ioError);
    if (ioError) {
        std::cerr << "Cannot create scratch directory for self-check\n";
        return 1;
    }

    try {
        infraforge::persistence::SqliteProjectStore store;

        infraforge::domain::project::CreateProjectSpec spec;
        spec.displayName = "Self Check";
        spec.parentDirectory = parentDirectory;
        spec.trafficSide = infraforge::domain::project::TrafficSide::Right;
        spec.georeference.horizontalCrs = "EPSG:32633";
        spec.georeference.linearUnit = "metre";
        spec.georeference.axisConvention = infraforge::domain::geo::AxisConvention::EastingNorthingUp;
        spec.georeference.originEasting = 500000.0;
        spec.georeference.originNorthing = 4649776.0;

        const auto created = store.create(spec);
        if (!store.isOpen() || created.uuid.empty() || created.revision != 1) {
            std::cerr << "Project creation self-check failed\n";
            return 1;
        }
        const auto projectDirectory = std::filesystem::path(created.directory);
        store.close();

        const auto reopened = store.open(projectDirectory);
        if (reopened.uuid != created.uuid || reopened.displayName != created.displayName
            || reopened.revision != created.revision
            || !(reopened.georeference == created.georeference)) {
            std::cerr << "Project reopen self-check failed\n";
            return 1;
        }

        store.close();
    } catch (const std::exception& error) {
        std::cerr << "Project persistence self-check failed: " << error.what() << '\n';
        std::filesystem::remove_all(scratchRoot, ioError);
        return 1;
    }

    // 3. Canonical georeference resolution and a control-point transform
    //    through the Geo transform service (verifies the geospatial runtime
    //    database is reachable).
    try {
        infraforge::domain::geo::GeoTransformService transforms;

        infraforge::domain::geo::GeoreferenceConfig config;
        config.horizontalCrs = "EPSG:32633";
        config.linearUnit = "metre";
        config.axisConvention = infraforge::domain::geo::AxisConvention::EastingNorthingUp;
        config.originEasting = 500000.0;
        config.originNorthing = 6094791.42;
        const auto project = transforms.resolveProjectGeoreference(config);

        // UTM zone 33N central meridian: lon 15 -> easting 500000 and
        // northing = 0.9996 * meridional arc (55 deg) ~= 6094791.42 m.
        const auto position = transforms.sourceToProjectGlobal(
            project,
            infraforge::domain::geo::SourceSpatialReference{.horizontalCrs = "EPSG:4326", .verticalCrs = ""},
            infraforge::domain::geo::GeoCoordinate{15.0, 55.0, 0.0});
        if (std::abs(position.easting - 500000.0) > 0.001
            || std::abs(position.northing - 6094791.42) > 0.001) {
            std::cerr << "Georeference transform self-check failed\n";
            return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << "Georeference self-check failed: " << error.what() << '\n';
        std::filesystem::remove_all(scratchRoot, ioError);
        return 1;
    }

    // 4. Full terrain vertical (real GDAL raster -> import job -> canonical
    //    sampling -> derived tiles -> reopen round-trip).
    if (run_terrain_self_check(scratchRoot / "terrain") != 0) {
        std::filesystem::remove_all(scratchRoot, ioError);
        return 1;
    }

    std::filesystem::remove_all(scratchRoot, ioError);

    std::cout
        << "{\"component\":\"" << infraforge::kEngineExecutableName
        << "\",\"version\":\"" << infraforge::kEngineVersion
        << "\",\"protocolMajor\":" << infraforge::kProtocolMajor
        << ",\"protocolMinor\":" << infraforge::kProtocolMinor
        << ",\"status\":\"ok\"}\n";
    return 0;
}

int run_server(const ServeArguments& arguments) {
    infraforge::persistence::SqliteProjectStore store;
    infraforge::network::WebSocketCommandRouter router;
    infraforge::domain::geo::GeoTransformService geoTransforms;
    infraforge::application::CommandProcessor processor(store, geoTransforms, router);
    processor.start();

    const int exitCode = infraforge::network::WebSocketServer(
        {
            .host = arguments.host,
            .port = arguments.port,
            .session_token = arguments.session_token,
        },
        processor,
        router).run();

    processor.shutdown();
    if (store.isOpen()) {
        // The graceful project-aware shutdown protocol is a later
        // milestone; flush and close the session so SQLite exits cleanly.
        const auto projectUuid = store.current().uuid;
        store.close();
        infraforge::runtime::logInfo("engine", "project.session_closed_at_shutdown", {{"projectUuid", projectUuid}});
    }
    return exitCode;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }

    const std::string_view command{argv[1]};

    if (command == "--version" && argc == 2) {
        std::cout << infraforge::kEngineExecutableName << ' ' << infraforge::kEngineVersion << '\n';
        return 0;
    }

    if (command == "--self-check" && argc == 2) {
        return run_self_check();
    }

    if ((command == "--help" || command == "-h") && argc == 2) {
        print_usage();
        return 0;
    }

    if (command == "--serve") {
        const auto arguments = parse_serve_arguments(argc, argv);
        if (!arguments.has_value()) {
            std::cerr << "Invalid --serve arguments. Host must be 127.0.0.1 and session token must be 64 hexadecimal characters.\n";
            print_usage();
            return 2;
        }

        try {
            return run_server(*arguments);
        } catch (const std::exception& error) {
            std::cerr << "Engine startup failed: " << error.what() << '\n';
            return 1;
        }
    }

    std::cerr << "Unknown command or invalid arguments\n";
    print_usage();
    return 2;
}
