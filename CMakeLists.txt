cmake_minimum_required(VERSION 3.25)

project(InfraForge VERSION 0.1.0 LANGUAGES CXX)

option(INFRAFORGE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(MSVC)
    add_compile_options(/MP)
endif()

enable_testing()

add_subdirectory(engine)
add_subdirectory(viewport)
