find_package(doctest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)

set(INFRAFORGE_VIEWPORT_PLATFORM_SOURCES src/platform/SurfaceInput.cpp src/platform/SurfaceUnsupported.cpp)
if(WIN32)
    set(INFRAFORGE_VIEWPORT_PLATFORM_SOURCES src/platform/SurfaceInput.cpp src/platform/SurfaceWin32.cpp)
endif()

set(INFRAFORGE_VIEWPORT_RENDERER_SOURCES
    src/renderer/EditorCamera.cpp
    src/renderer/EditorCameraController.cpp
    src/renderer/GridPass.cpp
    src/renderer/SelectionId.cpp
    src/renderer/SwapchainState.cpp
    src/renderer/TerrainPass.cpp
    src/renderer/TerrainScene.cpp
    src/renderer/TerrainTileCache.cpp
    src/renderer/VulkanDevice.cpp
    src/renderer/VulkanInstance.cpp
    src/renderer/VulkanRenderer.cpp
    src/renderer/VulkanSurface.cpp
    src/renderer/VulkanSwapchain.cpp
)

set(INFRAFORGE_VIEWPORT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${INFRAFORGE_VIEWPORT_GENERATED_DIR})

add_custom_command(
    OUTPUT ${INFRAFORGE_VIEWPORT_GENERATED_DIR}/ViewportShaderSource.h
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${INFRAFORGE_VIEWPORT_GENERATED_DIR}/ViewportShaderSource.h
        -DNS=shaders
        -DNAME_0=kGridVertex -DFILE_0=${CMAKE_CURRENT_SOURCE_DIR}/../shaders/grid.vert.glsl
        -DNAME_1=kGridFragment -DFILE_1=${CMAKE_CURRENT_SOURCE_DIR}/../shaders/grid.frag.glsl
        -DNAME_2=kTerrainVertex -DFILE_2=${CMAKE_CURRENT_SOURCE_DIR}/../shaders/terrain.vert.glsl
        -DNAME_3=kTerrainFragment -DFILE_3=${CMAKE_CURRENT_SOURCE_DIR}/../shaders/terrain.frag.glsl
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/EmbedTextFiles.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/grid.vert.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/grid.frag.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/terrain.vert.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/terrain.frag.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/EmbedTextFiles.cmake
    COMMENT "Embedding viewport shader sources"
)

add_library(infraforge_viewport_core STATIC
    src/control/ControlProtocol.cpp
    src/ViewportApplication.cpp
    ${INFRAFORGE_VIEWPORT_PLATFORM_SOURCES}
    ${INFRAFORGE_VIEWPORT_RENDERER_SOURCES}
    ${INFRAFORGE_VIEWPORT_GENERATED_DIR}/ViewportShaderSource.h
)

target_compile_features(infraforge_viewport_core PUBLIC cxx_std_23)

target_include_directories(infraforge_viewport_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${INFRAFORGE_VIEWPORT_GENERATED_DIR}
)

target_link_libraries(infraforge_viewport_core PUBLIC
    infraforge_runtime
    nlohmann_json::nlohmann_json
    Vulkan::Vulkan
    unofficial::shaderc::shaderc
)

add_executable(infraforge-viewport src/main.cpp)
target_compile_features(infraforge-viewport PRIVATE cxx_std_23)
target_link_libraries(infraforge-viewport PRIVATE infraforge_viewport_core)

add_executable(infraforge-viewport-tests
    tests/test_main.cpp
    tests/CameraTests.cpp
    tests/ControlProtocolTests.cpp
    tests/RenderThreadTests.cpp
    tests/SelectionIdTests.cpp
    tests/SwapchainStateTests.cpp
    tests/SurfaceInputTests.cpp
    tests/TerrainSceneTests.cpp
    tests/TerrainTileCacheTests.cpp
    tests/VulkanFlushRangeTests.cpp
    tests/ViewportApplicationTests.cpp
    tests/Win32DpiTests.cpp
)
target_compile_features(infraforge-viewport-tests PRIVATE cxx_std_23)
target_link_libraries(infraforge-viewport-tests PRIVATE
    infraforge_viewport_core
    doctest::doctest
)

add_test(NAME infraforge-viewport-tests COMMAND infraforge-viewport-tests)

if(WIN32)
    target_link_libraries(infraforge_viewport_core PRIVATE user32 gdi32)
    # Windows 10 SDK surface: per-monitor-v2 DPI awareness APIs and context
    # queries require _WIN32_WINNT >= 0x0605.
    foreach(target infraforge_viewport_core infraforge-viewport infraforge-viewport-tests)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    endforeach()
endif()

if(MSVC)
    foreach(target infraforge_viewport_core infraforge-viewport infraforge-viewport-tests)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:preprocessor)
        if(INFRAFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endforeach()
else()
    foreach(target infraforge_viewport_core infraforge-viewport infraforge-viewport-tests)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(INFRAFORGE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endforeach()
endif()
