option(SENCHA_ENABLE_VULKAN
    "Build Sencha Vulkan graphics foundation"
    ON)

option(SENCHA_ENABLE_HOT_RELOAD
    "Link glslang into the engine for runtime GLSL compilation and live shader reload. Always OFF in release builds -- shipping binaries must not contain a GLSL compiler."
    OFF)

option(SENCHA_ENABLE_DEBUG_UI
    "Build the ImGui-based debug overlay frontend (ConsolePanel, ImGuiDebugOverlay). Requires SENCHA_ENABLE_VULKAN."
    OFF)

option(SENCHA_ENABLE_COOK
    "Build the dev-only asset cook layer (import-on-demand, cooked cache, importers). Always OFF in shipping builds -- cooked data ships, importers do not."
    ON)

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
        "[/\\\\]src[/\\\\]debug[/\\\\]TimingPanel\\.cpp$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ConsolePanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]IDebugPanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ImGuiDebugOverlay\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]TimingPanel\\.h$"
    )
        list(FILTER SENCHA_ENGINE_SOURCES EXCLUDE REGEX "${_pattern}")
        list(FILTER SENCHA_ENGINE_HEADERS EXCLUDE REGEX "${_pattern}")
    endforeach()
endif()

if(NOT SENCHA_ENABLE_DEBUG_UI)
    foreach(_pattern
        "[/\\\\]src[/\\\\]debug[/\\\\]ConsolePanel\\.cpp$"
        "[/\\\\]src[/\\\\]debug[/\\\\]ImGuiDebugOverlay\\.cpp$"
        "[/\\\\]src[/\\\\]debug[/\\\\]TimingPanel\\.cpp$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ConsolePanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]IDebugPanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ImGuiDebugOverlay\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]TimingPanel\\.h$"
    )
        list(FILTER SENCHA_ENGINE_SOURCES EXCLUDE REGEX "${_pattern}")
        list(FILTER SENCHA_ENGINE_HEADERS EXCLUDE REGEX "${_pattern}")
    endforeach()
endif()

if(NOT SENCHA_ENABLE_COOK)
    # Hot reload (Stage 6) re-cooks on source change, so it is dev-only too —
    # filtered out alongside the cook layer it depends on.
    foreach(_pattern
        "[/\\\\]src[/\\\\]assets[/\\\\]cook[/\\\\]"
        "[/\\\\]include[/\\\\]assets[/\\\\]cook[/\\\\]"
        "[/\\\\]src[/\\\\]assets[/\\\\]hotreload[/\\\\]"
        "[/\\\\]include[/\\\\]assets[/\\\\]hotreload[/\\\\]"
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

if(SENCHA_ENABLE_COOK)
    target_compile_definitions(sencha_engine PUBLIC SENCHA_ENABLE_COOK)

    # BC block encoders for the texture cook (docs/assets/pipeline.md,
    # Decisions E/L): bc7enc (BC7) + rgbcx (BC4/BC5) + bc7decomp (tests
    # decode-verify the encoder). MIT, single-purpose — the stb precedent.
    # Cook-only: never rides in a shipping binary. Populated without
    # add_subdirectory because the repo's own CMakeLists builds a test app.
    # bc7enc.c is plain C — the only C in the tree, hence the enable here.
    enable_language(C)
    include(FetchContent)
    FetchContent_Declare(
        bc7enc
        GIT_REPOSITORY https://github.com/richgel999/bc7enc.git
        GIT_TAG        f66c2e489b07138f2673a2fb3d27c1aa1d565c48
    )
    FetchContent_GetProperties(bc7enc)
    if(NOT bc7enc_POPULATED)
        FetchContent_Populate(bc7enc)
    endif()
    target_sources(sencha_engine PRIVATE
        "${bc7enc_SOURCE_DIR}/bc7enc.c"
        "${bc7enc_SOURCE_DIR}/bc7decomp.cpp"
    )
    target_include_directories(sencha_engine PUBLIC "${bc7enc_SOURCE_DIR}")

    # glTF parsing for the mesh cook (Decision B): cgltf, single-header,
    # the stb precedent. The implementation TU lives in src/assets/cook.
    FetchContent_Declare(
        cgltf
        GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
        GIT_TAG        bbeb5b0b070ddacddac6852fb72143eb68454937 # v1.15
    )
    FetchContent_GetProperties(cgltf)
    if(NOT cgltf_POPULATED)
        FetchContent_Populate(cgltf)
    endif()
    target_include_directories(sencha_engine PUBLIC "${cgltf_SOURCE_DIR}")

    # Tangent generation for the mesh cook (Decision M): MikkTSpace, the
    # de-facto standard. Cook-only, never ships — the glslang rule.
    FetchContent_Declare(
        mikktspace
        GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
        GIT_TAG        3e895b49d05ea07e4c2133156cfa94369e19e409
    )
    FetchContent_GetProperties(mikktspace)
    if(NOT mikktspace_POPULATED)
        FetchContent_Populate(mikktspace)
    endif()
    target_sources(sencha_engine PRIVATE "${mikktspace_SOURCE_DIR}/mikktspace.c")
    target_include_directories(sencha_engine PUBLIC "${mikktspace_SOURCE_DIR}")
endif()

if(WIN32)
    # winmm for timeBeginPeriod/timeEndPeriod used by FramePacer to achieve
    # 1ms sleep granularity on Windows.
    target_link_libraries(sencha_engine PRIVATE winmm)
endif()

include(FetchContent)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        ae721c50eaf761660b4f90cc590453cdb0c2acd0
)
FetchContent_MakeAvailable(stb)
target_include_directories(sencha_engine PRIVATE "${stb_SOURCE_DIR}")

if(SENCHA_ENABLE_VULKAN)
    target_compile_definitions(sencha_engine PUBLIC SENCHA_ENABLE_VULKAN)

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
