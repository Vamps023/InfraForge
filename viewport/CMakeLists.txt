find_package(doctest CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(unofficial-shaderc CONFIG REQUIRED)

set(INFRAFORGE_VIEWPORT_PLATFORM_SOURCES src/platform/SurfaceUnsupported.cpp)
if(WIN32)
    set(INFRAFORGE_VIEWPORT_PLATFORM_SOURCES src/platform/SurfaceWin32.cpp)
endif()

set(INFRAFORGE_VIEWPORT_RENDERER_SOURCES
    src/renderer/GridCamera.cpp
    src/renderer/GridPass.cpp
    src/renderer/SelectionId.cpp
    src/renderer/SwapchainState.cpp
    src/renderer/VulkanDevice.cpp
    src/renderer/VulkanInstance.cpp
    src/renderer/VulkanRenderer.cpp
    src/renderer/VulkanSurface.cpp
    src/renderer/VulkanSwapchain.cpp
)

set(INFRAFORGE_VIEWPORT_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${INFRAFORGE_VIEWPORT_GENERATED_DIR})

add_custom_command(
    OUTPUT ${INFRAFORGE_VIEWPORT_GENERATED_DIR}/GridShaderSource.h
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${INFRAFORGE_VIEWPORT_GENERATED_DIR}/GridShaderSource.h
        -DNS=shaders
        -DNAME_0=kGridVertex -DFILE_0=${CMAKE_CURRENT_SOURCE_DIR}/../shaders/grid.vert.glsl
        -DNAME_1=kGridFragment -DFILE_1=${CMAKE_CURRENT_SOURCE_DIR}/../shaders/grid.frag.glsl
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/EmbedTextFiles.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/grid.vert.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/grid.frag.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/../shaders/EmbedTextFiles.cmake
    COMMENT "Embedding grid shader sources"
)

add_library(infraforge_viewport_core STATIC
    src/control/ControlProtocol.cpp
    src/ViewportApplication.cpp
    ${INFRAFORGE_VIEWPORT_PLATFORM_SOURCES}
    ${INFRAFORGE_VIEWPORT_RENDERER_SOURCES}
    ${INFRAFORGE_VIEWPORT_GENERATED_DIR}/GridShaderSource.h
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

if(WIN32)
    target_link_libraries(infraforge_viewport_core PRIVATE user32 gdi32)
    target_compile_definitions(infraforge_viewport_core PRIVATE UNICODE _UNICODE)
    target_compile_definitions(infraforge-viewport PRIVATE UNICODE _UNICODE)
endif()

add_executable(infraforge-viewport-tests
    tests/test_main.cpp
    tests/CameraTests.cpp
    tests/ControlProtocolTests.cpp
    tests/SelectionIdTests.cpp
    tests/SwapchainStateTests.cpp
)
target_compile_features(infraforge-viewport-tests PRIVATE cxx_std_23)
target_link_libraries(infraforge-viewport-tests PRIVATE
    infraforge_viewport_core
    doctest::doctest
)

add_test(NAME infraforge-viewport-tests COMMAND infraforge-viewport-tests)

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
