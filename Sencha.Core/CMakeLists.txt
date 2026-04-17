option(SENCHA_ENABLE_VULKAN
    "Build Sencha Vulkan graphics foundation"
    ON)

option(SENCHA_ENABLE_HOT_RELOAD
    "Link glslang into the engine for runtime GLSL compilation and live shader reload. Always OFF in release builds -- shipping binaries must not contain a GLSL compiler."
    OFF)

set(SENCHA_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SENCHA_CORE_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE SENCHA_CORE_SOURCES CONFIGURE_DEPENDS
    "${SENCHA_CORE_SOURCE_DIR}/*.cpp"
)
file(GLOB_RECURSE SENCHA_CORE_HEADERS CONFIGURE_DEPENDS
    "${SENCHA_CORE_INCLUDE_DIR}/*.h"
)

if(NOT SENCHA_ENABLE_VULKAN)
    foreach(_pattern
        "[/\\\\]src[/\\\\]graphics[/\\\\]"
        "[/\\\\]src[/\\\\]assets[/\\\\]texture[/\\\\]TextureCache\\.cpp$"
    )
        list(FILTER SENCHA_CORE_SOURCES EXCLUDE REGEX "${_pattern}")
    endforeach()

    foreach(_pattern
        "[/\\\\]include[/\\\\]graphics[/\\\\]"
        "[/\\\\]include[/\\\\]assets[/\\\\]texture[/\\\\]TextureCache\\.h$"
    )
        list(FILTER SENCHA_CORE_HEADERS EXCLUDE REGEX "${_pattern}")
    endforeach()
endif()

find_package(SDL3 REQUIRED CONFIG)

add_library(sencha_core STATIC
    ${SENCHA_CORE_SOURCES}
    ${SENCHA_CORE_HEADERS}
)
add_library(sencha::core ALIAS sencha_core)

target_include_directories(sencha_core
    PUBLIC "${SENCHA_CORE_INCLUDE_DIR}"
)
target_link_libraries(sencha_core PUBLIC SDL3::SDL3)

include(FetchContent)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        ae721c50eaf761660b4f90cc590453cdb0c2acd0
)
FetchContent_MakeAvailable(stb)
target_include_directories(sencha_core PRIVATE "${stb_SOURCE_DIR}")

if(SENCHA_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(sencha_core PUBLIC Vulkan::Vulkan)

    set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(VMA_BUILD_SAMPLES       OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.2.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    target_link_libraries(sencha_core PUBLIC GPUOpen::VulkanMemoryAllocator)

    if(SENCHA_ENABLE_HOT_RELOAD)
        target_compile_definitions(sencha_core PUBLIC SENCHA_ENABLE_HOT_RELOAD)

        set(ENABLE_HLSL             OFF CACHE BOOL "" FORCE)
        set(ENABLE_CTEST            OFF CACHE BOOL "" FORCE)
        set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
        set(BUILD_EXTERNAL          OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING           OFF CACHE BOOL "" FORCE)
        set(SKIP_GLSLANG_INSTALL    ON  CACHE BOOL "" FORCE)
        set(ENABLE_OPT              OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            glslang
            GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
            GIT_TAG        14.3.0
        )
        FetchContent_MakeAvailable(glslang)
        target_link_libraries(sencha_core
            PUBLIC
                glslang::glslang
                glslang::SPIRV
                glslang-default-resource-limits
        )
    endif()
endif()
