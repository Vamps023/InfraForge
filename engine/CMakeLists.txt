find_package(ixwebsocket CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(PROJ CONFIG REQUIRED)
find_package(GDAL CONFIG REQUIRED)
find_package(PNG REQUIRED)

set(INFRAFORGE_PROTOCOL_PROTOS
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/foundation.proto
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/geo.proto
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/project.proto
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/terrain.proto
)

set(INFRAFORGE_PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
# CMake >= 4 no longer creates the protobuf generation output directory.
file(MAKE_DIRECTORY ${INFRAFORGE_PROTO_BINARY_DIR})

add_library(infraforge_runtime STATIC
    src/runtime/FileSystemUtf8.cpp
    src/runtime/Logging.cpp
    src/runtime/Sha256.cpp
    src/runtime/Timestamp.cpp
    src/runtime/Uuid.cpp
)

target_compile_features(infraforge_runtime PUBLIC cxx_std_23)

target_include_directories(infraforge_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(infraforge_core STATIC
    src/application/CommandProcessor.cpp
    src/application/GeoService.cpp
    src/application/JobSystem.cpp
    src/application/ProjectService.cpp
    src/application/TerrainService.cpp
    src/application/TerrainTileGenerator.cpp
    src/application/WorldState.cpp
    src/domain/geo/GeoreferenceConfig.cpp
    src/domain/geo/GeoTransformService.cpp
    src/domain/geo/ProjectGeoreference.cpp
    src/domain/project/ProjectModel.cpp
    src/domain/terrain/TerrainDataset.cpp
    src/domain/terrain/TerrainDownloadProvider.cpp
    src/domain/terrain/TerrainSampler.cpp
    src/domain/terrain/MockTerrainProvider.cpp
    src/domain/terrain/TerrariumTerrainProvider.cpp
    src/domain/road/RoadTypes.cpp
    src/domain/world/ChunkGrid.cpp
    src/domain/world/ChunkResidency.cpp
    src/domain/world/Invalidation.cpp
    src/domain/world/SpatialIndex.cpp
    src/persistence/GdalTerrainSource.cpp
    src/persistence/ProjectManifest.cpp
    src/persistence/SchemaMigrations.cpp
    src/persistence/SqliteConnection.cpp
    src/persistence/SqliteProjectStore.cpp
    ${INFRAFORGE_PROTOCOL_PROTOS}
)

target_compile_features(infraforge_core PUBLIC cxx_std_23)

target_include_directories(infraforge_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${INFRAFORGE_PROTO_BINARY_DIR}
)

target_link_libraries(infraforge_core PUBLIC
    infraforge_runtime
    protobuf::libprotobuf
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
    PROJ::proj
    GDAL::GDAL
    PNG::PNG
)

# The geospatial runtime locates its PROJ data directory (proj.db) through
# PROJ_DATA/PROJ_LIB first; the vcpkg share layout is the compiled-in
# fallback for development builds, and deployed layouts probe
# executable-relative share/proj.
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(INFRAFORGE_PROJ_SHARE_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/proj")
    # PUBLIC so the test fixtures can point GDAL's own PROJ context at the
    # same proj.db the engine service uses (PROJ_DATA environment).
    target_compile_definitions(infraforge_core PUBLIC
        INFRAFORGE_PROJ_DATA_DIR="${INFRAFORGE_PROJ_SHARE_DIR}")
endif()

protobuf_generate(
    TARGET infraforge_core
    IMPORT_DIRS ${PROJECT_SOURCE_DIR}/contracts/proto
    PROTOC_OUT_DIR ${INFRAFORGE_PROTO_BINARY_DIR}
)

add_executable(infraforge-engine
    src/main.cpp
    src/network/CommandRouter.cpp
    src/network/WebSocketServer.cpp
)

target_compile_features(infraforge-engine PRIVATE cxx_std_23)

target_link_libraries(infraforge-engine PRIVATE
    infraforge_core
    ixwebsocket::ixwebsocket
)

if(WIN32)
    # mbedTLS (pulled in by ixwebsocket) uses the Windows BCrypt RNG.
    target_link_libraries(infraforge-engine PRIVATE bcrypt)
endif()

# Deployed builds need proj.db beside the engine: stage it next to the
# binary after every build and install it for packaging.
if(DEFINED INFRAFORGE_PROJ_SHARE_DIR AND EXISTS "${INFRAFORGE_PROJ_SHARE_DIR}/proj.db")
    add_custom_command(TARGET infraforge-engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${INFRAFORGE_PROJ_SHARE_DIR}" "$<TARGET_FILE_DIR:infraforge-engine>/share/proj")
    install(TARGETS infraforge-engine RUNTIME DESTINATION bin)
    install(DIRECTORY "${INFRAFORGE_PROJ_SHARE_DIR}" DESTINATION share)
endif()

find_package(doctest CONFIG REQUIRED)

add_executable(infraforge-engine-tests
    tests/test_main.cpp
    tests/TestHelpers.cpp
    tests/ChunkCacheMetadataTests.cpp
    tests/ChunkGridTests.cpp
    tests/ChunkResidencyTests.cpp
    tests/CommandProcessorTests.cpp
    tests/GeoPersistenceTests.cpp
    tests/GeoServiceTests.cpp
    tests/GeoTransformServiceTests.cpp
    tests/JobSystemTests.cpp
    tests/LargeWorldIndexTests.cpp
    tests/ProjectModelTests.cpp
    tests/ProjectServiceTests.cpp
    tests/RoadDomainTests.cpp
    tests/SpatialBoundsTests.cpp
    tests/SpatialIndexTests.cpp
    tests/SqliteProjectStoreTests.cpp
    tests/TerrainDomainTests.cpp
    tests/TerrainDownloadProviderTests.cpp
    tests/TerrainServiceTests.cpp
    tests/TerrariumProviderTests.cpp
    tests/WebSocketServerTests.cpp
    src/network/CommandRouter.cpp
    src/network/WebSocketServer.cpp
)

target_compile_features(infraforge-engine-tests PRIVATE cxx_std_23)

target_link_libraries(infraforge-engine-tests PRIVATE
    infraforge_core
    doctest::doctest
    ixwebsocket::ixwebsocket
)

if(WIN32)
    target_link_libraries(infraforge-engine-tests PRIVATE bcrypt)
endif()

if(MSVC)
    foreach(target IN ITEMS infraforge_runtime infraforge_core infraforge-engine infraforge-engine-tests)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:preprocessor)
        if(INFRAFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endforeach()
else()
    foreach(target IN ITEMS infraforge_runtime infraforge_core infraforge-engine infraforge-engine-tests)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(INFRAFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endforeach()
endif()

add_test(NAME infraforge-engine-tests COMMAND infraforge-engine-tests)
