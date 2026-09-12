find_package(ixwebsocket CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

set(INFRAFORGE_PROTOCOL_PROTOS
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/foundation.proto
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/project.proto
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/validation.proto
)

set(INFRAFORGE_PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
# CMake >= 4 no longer creates the protobuf generation output directory.
file(MAKE_DIRECTORY ${INFRAFORGE_PROTO_BINARY_DIR})

add_library(infraforge_core STATIC
    src/application/CommandProcessor.cpp
    src/application/ProjectService.cpp
    src/application/validation/DiagnosticStore.cpp
    src/application/validation/ValidationService.cpp
    src/application/validation/ValidatorRegistry.cpp
    src/application/validation/ProjectFoundationValidator.cpp
    src/domain/project/ProjectModel.cpp
    src/domain/validation/Diagnostic.cpp
    src/persistence/ProjectManifest.cpp
    src/persistence/SchemaMigrations.cpp
    src/persistence/SqliteConnection.cpp
    src/persistence/SqliteProjectStore.cpp
    src/runtime/FileSystemUtf8.cpp
    src/runtime/Logging.cpp
    src/runtime/Timestamp.cpp
    src/runtime/Uuid.cpp
    ${INFRAFORGE_PROTOCOL_PROTOS}
)

target_compile_features(infraforge_core PUBLIC cxx_std_23)

target_include_directories(infraforge_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${INFRAFORGE_PROTO_BINARY_DIR}
)

target_link_libraries(infraforge_core PUBLIC
    protobuf::libprotobuf
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
)

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

if(MSVC)
    target_compile_options(infraforge_core PRIVATE /W4 /permissive- /Zc:preprocessor)
    target_compile_options(infraforge-engine PRIVATE /W4 /permissive- /Zc:preprocessor)
    if(INFRAFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(infraforge_core PRIVATE /WX)
        target_compile_options(infraforge-engine PRIVATE /WX)
    endif()
else()
    target_compile_options(infraforge_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(infraforge-engine PRIVATE -Wall -Wextra -Wpedantic)
    if(INFRAFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(infraforge_core PRIVATE -Werror)
        target_compile_options(infraforge-engine PRIVATE -Werror)
    endif()
endif()

find_package(doctest CONFIG REQUIRED)

add_executable(infraforge-engine-tests
    tests/test_main.cpp
    tests/TestHelpers.cpp
    tests/CommandProcessorTests.cpp
    tests/ProjectModelTests.cpp
    tests/ProjectServiceTests.cpp
    tests/SqliteProjectStoreTests.cpp
    tests/WebSocketServerTests.cpp
    tests/ValidationTests.cpp
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
    target_compile_options(infraforge-engine-tests PRIVATE /W4 /permissive- /Zc:preprocessor)
    if(INFRAFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(infraforge-engine-tests PRIVATE /WX)
    endif()
else()
    target_compile_options(infraforge-engine-tests PRIVATE -Wall -Wextra -Wpedantic)
    if(INFRAFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(infraforge-engine-tests PRIVATE -Werror)
    endif()
endif()

add_test(NAME infraforge-engine-tests COMMAND infraforge-engine-tests)
