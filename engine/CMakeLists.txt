find_package(ixwebsocket CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)

set(INFRAFORGE_FOUNDATION_PROTO
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/foundation.proto
    ${PROJECT_SOURCE_DIR}/contracts/proto/infraforge/protocol/v1/project.proto
)

add_executable(infraforge-engine
    src/main.cpp
    src/network/WebSocketServer.cpp
    ${INFRAFORGE_FOUNDATION_PROTO}
)

target_compile_features(infraforge-engine PRIVATE cxx_std_23)

target_include_directories(infraforge-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(infraforge-engine PRIVATE
    ixwebsocket::ixwebsocket
    protobuf::libprotobuf
)

if(WIN32)
    # mbedTLS (pulled in by ixwebsocket) uses the Windows BCrypt RNG.
    target_link_libraries(infraforge-engine PRIVATE bcrypt)
endif()

set(INFRAFORGE_PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
# CMake >= 4 no longer creates the protobuf generation output directory.
file(MAKE_DIRECTORY ${INFRAFORGE_PROTO_BINARY_DIR})
target_include_directories(infraforge-engine PRIVATE ${INFRAFORGE_PROTO_BINARY_DIR})
protobuf_generate(
    TARGET infraforge-engine
    IMPORT_DIRS ${PROJECT_SOURCE_DIR}/contracts/proto
    PROTOC_OUT_DIR ${INFRAFORGE_PROTO_BINARY_DIR}
)

if(MSVC)
    target_compile_options(infraforge-engine PRIVATE /W4 /permissive- /Zc:preprocessor)
    if(INFRAFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(infraforge-engine PRIVATE /WX)
    endif()
else()
    target_compile_options(infraforge-engine PRIVATE -Wall -Wextra -Wpedantic)
    if(INFRAFORGE_WARNINGS_AS_ERRORS)
        target_compile_options(infraforge-engine PRIVATE -Werror)
    endif()
endif()
