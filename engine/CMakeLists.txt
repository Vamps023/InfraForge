add_executable(infraforge-engine
    src/main.cpp
)

target_compile_features(infraforge-engine PRIVATE cxx_std_23)

target_include_directories(infraforge-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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
