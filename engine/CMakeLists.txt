option(SENCHA_ENABLE_VULKAN
    "Build Sencha Vulkan graphics foundation"
    ON)

option(SENCHA_ENABLE_HOT_RELOAD
    "Link glslang into the engine for runtime GLSL compilation and live shader reload. Always OFF in release builds -- shipping binaries must not contain a GLSL compiler."
    OFF)

option(SENCHA_ENABLE_DEBUG_UI
    "Build the ImGui-based debug overlay frontend (ConsolePanel, ImGuiDebugOverlay). Requires SENCHA_ENABLE_VULKAN."
    OFF)

if(SENCHA_ENABLE_DEBUG_UI AND NOT SENCHA_ENABLE_VULKAN)
    message(FATAL_ERROR "SENCHA_ENABLE_DEBUG_UI requires SENCHA_ENABLE_VULKAN=ON")
endif()

set(SENCHA_ENGINE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SENCHA_ENGINE_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE SENCHA_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${SENCHA_ENGINE_SOURCE_DIR}/*.cpp"
)
file(GLOB_RECURSE SENCHA_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${SENCHA_ENGINE_INCLUDE_DIR}/*.h"
)

include("${CMAKE_SOURCE_DIR}/cmake/SenchaShaders.cmake")

if(NOT SENCHA_ENABLE_VULKAN)
    foreach(_pattern
        "[/\\\\]src[/\\\\]graphics[/\\\\]vulkan[/\\\\]"
        "[/\\\\]include[/\\\\]graphics[/\\\\]vulkan[/\\\\]"
        "[/\\\\]src[/\\\\]debug[/\\\\]ConsolePanel\\.cpp$"
        "[/\\\\]src[/\\\\]debug[/\\\\]ImGuiDebugOverlay\\.cpp$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ConsolePanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]IDebugPanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ImGuiDebugOverlay\\.h$"
    )
        list(FILTER SENCHA_ENGINE_SOURCES EXCLUDE REGEX "${_pattern}")
        list(FILTER SENCHA_ENGINE_HEADERS EXCLUDE REGEX "${_pattern}")
    endforeach()
endif()

if(NOT SENCHA_ENABLE_DEBUG_UI)
    foreach(_pattern
        "[/\\\\]src[/\\\\]debug[/\\\\]ConsolePanel\\.cpp$"
        "[/\\\\]src[/\\\\]debug[/\\\\]ImGuiDebugOverlay\\.cpp$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ConsolePanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]IDebugPanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ImGuiDebugOverlay\\.h$"
    )
        list(FILTER SENCHA_ENGINE_SOURCES EXCLUDE REGEX "${_pattern}")
        list(FILTER SENCHA_ENGINE_HEADERS EXCLUDE REGEX "${_pattern}")
    endforeach()
endif()

set(SENCHA_ENGINE_GENERATED_HEADERS)

if(SENCHA_ENABLE_VULKAN)
    sencha_compile_shader(
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh_forward.vert.glsl"
        STAGE vert
        OUTPUT_SPV _mesh_forward_vert_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_mesh_forward_vert_spv}"
        VAR_NAME kMeshForwardVertSpv
        OUT_HEADER _mesh_forward_vert_header
    )
    sencha_compile_shader(
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh_forward.frag.glsl"
        STAGE frag
        OUTPUT_SPV _mesh_forward_frag_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_mesh_forward_frag_spv}"
        VAR_NAME kMeshForwardFragSpv
        OUT_HEADER _mesh_forward_frag_header
    )
    list(APPEND SENCHA_ENGINE_GENERATED_HEADERS
        "${_mesh_forward_vert_header}"
        "${_mesh_forward_frag_header}"
    )
endif()

find_package(SDL3 REQUIRED CONFIG)

add_library(sencha_engine STATIC
    ${SENCHA_ENGINE_SOURCES}
    ${SENCHA_ENGINE_HEADERS}
    ${SENCHA_ENGINE_GENERATED_HEADERS}
)
add_library(sencha::engine ALIAS sencha_engine)

target_include_directories(sencha_engine
    PUBLIC "${SENCHA_ENGINE_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(sencha_engine PUBLIC SDL3::SDL3)
target_compile_features(sencha_engine PUBLIC cxx_std_20)

include(FetchContent)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        ae721c50eaf761660b4f90cc590453cdb0c2acd0
)
FetchContent_MakeAvailable(stb)
target_include_directories(sencha_engine PRIVATE "${stb_SOURCE_DIR}")

if(SENCHA_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(sencha_engine PUBLIC Vulkan::Vulkan)

    set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(VMA_BUILD_SAMPLES       OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.2.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    target_link_libraries(sencha_engine PUBLIC GPUOpen::VulkanMemoryAllocator)

    if(SENCHA_ENABLE_HOT_RELOAD)
        target_compile_definitions(sencha_engine PUBLIC SENCHA_ENABLE_HOT_RELOAD)

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
        target_link_libraries(sencha_engine
            PUBLIC
                glslang::glslang
                glslang::SPIRV
                glslang-default-resource-limits
        )
    endif()

    if(SENCHA_ENABLE_DEBUG_UI)
        target_compile_definitions(sencha_engine PUBLIC SENCHA_ENABLE_DEBUG_UI)

        if(NOT IMGUI_DIR)
            FetchContent_Declare(
                imgui
                GIT_REPOSITORY https://github.com/ocornut/imgui.git
                GIT_TAG        v1.91.9
            )
            FetchContent_MakeAvailable(imgui)
            set(IMGUI_DIR "${imgui_SOURCE_DIR}")
        endif()

        target_sources(sencha_engine PRIVATE
            "${IMGUI_DIR}/imgui.cpp"
            "${IMGUI_DIR}/imgui_draw.cpp"
            "${IMGUI_DIR}/imgui_tables.cpp"
            "${IMGUI_DIR}/imgui_widgets.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
        )

        target_include_directories(sencha_engine
            PUBLIC
                "${IMGUI_DIR}"
                "${IMGUI_DIR}/backends"
        )
    endif()
endif()

add_library(engine ALIAS sencha_engine)
