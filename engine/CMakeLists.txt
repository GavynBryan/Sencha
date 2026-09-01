# Feature flags (SENCHA_ENABLE_VULKAN/DEBUG_UI/COOK/HOT_RELOAD) and their
# cross-option invariants are declared centrally in cmake/SenchaOptions.cmake,
# included from the root CMakeLists before this subdirectory is added.

set(SENCHA_ENGINE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SENCHA_ENGINE_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE SENCHA_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${SENCHA_ENGINE_SOURCE_DIR}/*.cpp"
)
file(GLOB_RECURSE SENCHA_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${SENCHA_ENGINE_INCLUDE_DIR}/*.h"
)

include("${CMAKE_SOURCE_DIR}/cmake/SenchaShaders.cmake")
include("${CMAKE_SOURCE_DIR}/cmake/SenchaComponentCodegen.cmake")

if(NOT SENCHA_ENABLE_VULKAN)
    # Only the Vulkan graphics subtree here. The ImGui debug panels are gated
    # below by SENCHA_ENABLE_DEBUG_UI, which the guard above forces OFF whenever
    # Vulkan is OFF -- so listing them here too would be dead duplication.
    foreach(_pattern
        "[/\\\\]src[/\\\\]graphics[/\\\\]vulkan[/\\\\]"
        "[/\\\\]include[/\\\\]graphics[/\\\\]vulkan[/\\\\]"
        "[/\\\\]src[/\\\\]debug[/\\\\]ConsolePanel\\.cpp$"
        "[/\\\\]src[/\\\\]debug[/\\\\]ConsoleView\\.cpp$"
        "[/\\\\]src[/\\\\]debug[/\\\\]ImGuiDebugOverlay\\.cpp$"
        "[/\\\\]src[/\\\\]debug[/\\\\]TimingPanel\\.cpp$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ConsolePanel\\.h$"
        "[/\\\\]include[/\\\\]debug[/\\\\]ConsoleView\\.h$"
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

if(NOT SENCHA_ENABLE_RENDER_PROFILING)
    # The mode enum, RenderInstrumentation (always-null when disabled), the
    # RenderStats aggregate, and the pass-local counter accumulation stay
    # compiled in every build; only the instrumentation bodies are removed.
    foreach(_pattern
        "[/\\\\]graphics[/\\\\]vulkan[/\\\\]GpuTimestampPool\\."
        "[/\\\\]graphics[/\\\\]vulkan[/\\\\]VulkanDebugLabels\\."
        "[/\\\\]profiling[/\\\\]RenderCapture\\."
        "[/\\\\]debug[/\\\\]RenderStatsPanel\\."
        "[/\\\\]render[/\\\\]RenderDebugView\\."
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
    if(SENCHA_ENABLE_RENDER_PROFILING)
        sencha_compile_shader(
            SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh_debug_view.frag.glsl"
            STAGE frag
            OUTPUT_SPV _mesh_debug_view_frag_spv
        )
        sencha_embed_spirv(
            SOURCE_SPV "${_mesh_debug_view_frag_spv}"
            VAR_NAME kMeshDebugViewFragSpv
            OUT_HEADER _mesh_debug_view_frag_header
        )
    endif()
    sencha_compile_shader(
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_depth.vert.glsl"
        STAGE vert
        OUTPUT_SPV _shadow_depth_vert_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_shadow_depth_vert_spv}"
        VAR_NAME kShadowDepthVertSpv
        OUT_HEADER _shadow_depth_vert_header
    )
    sencha_compile_shader(
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_depth.frag.glsl"
        STAGE frag
        OUTPUT_SPV _shadow_depth_frag_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_shadow_depth_frag_spv}"
        VAR_NAME kShadowDepthFragSpv
        OUT_HEADER _shadow_depth_frag_header
    )
    sencha_compile_shader(
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky_gradient.vert.glsl"
        STAGE vert
        OUTPUT_SPV _sky_gradient_vert_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_sky_gradient_vert_spv}"
        VAR_NAME kSkyGradientVertSpv
        OUT_HEADER _sky_gradient_vert_header
    )
    sencha_compile_shader(
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky_gradient.frag.glsl"
        STAGE frag
        OUTPUT_SPV _sky_gradient_frag_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_sky_gradient_frag_spv}"
        VAR_NAME kSkyGradientFragSpv
        OUT_HEADER _sky_gradient_frag_header
    )
    sencha_compile_shader(
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/skin_pose.comp.glsl"
        STAGE comp
        OUTPUT_SPV _skin_pose_comp_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_skin_pose_comp_spv}"
        VAR_NAME kSkinPoseCompSpv
        OUT_HEADER _skin_pose_comp_header
    )
    list(APPEND SENCHA_ENGINE_GENERATED_HEADERS
        "${_mesh_forward_vert_header}"
        "${_mesh_forward_frag_header}"
        "${_shadow_depth_vert_header}"
        "${_shadow_depth_frag_header}"
        "${_sky_gradient_vert_header}"
        "${_sky_gradient_frag_header}"
        "${_skin_pose_comp_header}"
    )
    if(SENCHA_ENABLE_RENDER_PROFILING)
        list(APPEND SENCHA_ENGINE_GENERATED_HEADERS
            "${_mesh_debug_view_frag_header}"
        )
    endif()
endif()

# --- Game-module ABI fingerprint (docs/plans/sencha-level-editor/09-module-abi-hardening.md) ---
# A hash of the module-facing ABI headers, embedded in both the engine and every
# game module (both include the generated header below). A module built against
# different ABI headers carries a different fingerprint, so the loader refuses it
# with a clear message instead of crashing on a skewed vtable.
#
# We hash the SEMANTIC content, not the raw bytes: comments are stripped and
# whitespace runs are collapsed to a single space before hashing, so editing a
# comment or reflowing a header in these dirs does NOT bump the fingerprint (and
# does not force every game module to rebuild). Any token change — a new member,
# a renamed type, a changed signature — still does. Whitespace is collapsed, not
# removed, so two distinct token streams can never normalize to the same string;
# and no ABI header here embeds a comment-introducer (// or /*) inside a string
# literal, so comment stripping never deletes a real token. Keep both true: it is
# what makes this safe (a false negative is the segfault we are eliminating).
#
# A directory belongs here when its headers can change the layout of something a
# game module compiles against. That is not only the types a module names: it
# includes anything Engine holds by value, because a module reaching a service
# through an inline accessor has the member offset baked into its own code. net/
# is here for exactly that reason -- Engine holds the replication, cvar-sync,
# command, traffic, and clock runtimes by value.
file(GLOB SENCHA_ABI_FINGERPRINT_HEADERS CONFIGURE_DEPENDS
    "${SENCHA_ENGINE_INCLUDE_DIR}/app/*.h"
    "${SENCHA_ENGINE_INCLUDE_DIR}/graphics/*.h"
    "${SENCHA_ENGINE_INCLUDE_DIR}/input/*.h"
    "${SENCHA_ENGINE_INCLUDE_DIR}/net/*.h"
    "${SENCHA_ENGINE_INCLUDE_DIR}/world/serialization/*.h"
    "${SENCHA_ENGINE_INCLUDE_DIR}/core/metadata/*.h"
    "${SENCHA_ENGINE_INCLUDE_DIR}/core/console/*.h"
    "${SENCHA_ENGINE_INCLUDE_DIR}/ecs/*.h"
)
list(SORT SENCHA_ABI_FINGERPRINT_HEADERS)
set(_sencha_abi_accum "")
foreach(_abi_hdr ${SENCHA_ABI_FINGERPRINT_HEADERS})
    file(READ "${_abi_hdr}" _abi_hdr_text)
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " _abi_hdr_text "${_abi_hdr_text}") # block comments
    string(REGEX REPLACE "//[^\n]*" "" _abi_hdr_text "${_abi_hdr_text}")                    # line comments
    string(REGEX REPLACE "[ \t\r\n]+" " " _abi_hdr_text "${_abi_hdr_text}")                 # collapse whitespace
    string(STRIP "${_abi_hdr_text}" _abi_hdr_text)
    string(SHA256 _abi_hdr_hash "${_abi_hdr_text}")
    string(APPEND _sencha_abi_accum "${_abi_hdr_hash}")
endforeach()
string(SHA256 _sencha_abi_full "${_sencha_abi_accum}")
string(SUBSTRING "${_sencha_abi_full}" 0 16 _sencha_abi_fp)
set(SENCHA_GAME_ABI_FINGERPRINT "0x${_sencha_abi_fp}ULL")
file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated-public/app/GameModuleAbiFingerprint.h"
    CONTENT "#pragma once
// Generated by engine/CMakeLists.txt — do not edit. Hash of the module-facing
// ABI headers; see 09-module-abi-hardening.md.
#define SENCHA_GAME_ABI_FINGERPRINT @SENCHA_GAME_ABI_FINGERPRINT@
"
    @ONLY)
# Reconfigure (and so re-hash) when any ABI header's content changes, not just
# when files are added/removed.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SENCHA_ABI_FINGERPRINT_HEADERS})

# --- Engine SDK version (engine-roadmap.md: SDK boundary, shipped) ---
# The semantic version of the published product, sourced from the root project()
# VERSION. A game module includes this to record which engine SDK it targets;
# the install's package version file (SenchaConfigVersion.cmake) uses the same
# numbers so find_package(Sencha <version>) can enforce compatibility. This is
# author-set and stable across rebuilds; it is NOT the ABI fingerprint above.
file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated-public/app/SenchaSdkVersion.h"
    CONTENT "#pragma once
// Generated by engine/CMakeLists.txt from the root project() VERSION -- do not
// edit. The engine SDK version; see engine-roadmap.md: SDK boundary, shipped.
#define SENCHA_SDK_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define SENCHA_SDK_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define SENCHA_SDK_VERSION_PATCH @PROJECT_VERSION_PATCH@
#define SENCHA_SDK_VERSION_STRING \"@PROJECT_VERSION@\"
"
    @ONLY)

find_package(SDL3 REQUIRED CONFIG)

add_library(sencha_engine SHARED
    ${SENCHA_ENGINE_SOURCES}
    ${SENCHA_ENGINE_HEADERS}
    ${SENCHA_ENGINE_GENERATED_HEADERS}
)
add_library(sencha::engine ALIAS sencha_engine)

# The ELF and MinGW builds export every non-hidden symbol from the shared
# engine, and consumers are built against that surface. MSVC exports nothing
# without a declspec or a module definition file, so let CMake generate the
# export table and keep one surface across toolchains. Game modules are a
# separate case: they export one factory through SENCHA_GAME_EXPORT.
set_target_properties(sencha_engine PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Mirror the on-disk src/ and include/ layout in IDE project trees (Visual
# Studio, Xcode, Qt Creator). Cosmetic for Ninja/Make. Generated headers live
# under the binary dir and are intentionally left out of the source-tree group.
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${SENCHA_ENGINE_SOURCES}
    ${SENCHA_ENGINE_HEADERS}
)

# BUILD_INTERFACE/INSTALL_INTERFACE so install(EXPORT) below does not bake
# absolute build-tree paths into the exported target. In-tree consumers see the
# source/generated dirs; an installed SDK sees the staged include dir.
target_include_directories(sencha_engine
    PUBLIC "$<BUILD_INTERFACE:${SENCHA_ENGINE_INCLUDE_DIR}>"
           "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated-public>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(sencha_engine PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})
target_compile_features(sencha_engine PUBLIC cxx_std_20)

sencha_warnings(sencha_engine)

sencha_generate_component_metadata(sencha_engine
    INCLUDE_ROOT "${SENCHA_ENGINE_INCLUDE_DIR}"
    HEADERS
        "${SENCHA_ENGINE_INCLUDE_DIR}/anim/AnimationClipPlayerComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/audio/AudioCaptionComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/audio/AudioSourceComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/camera/CameraRig.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/camera/CameraSeat.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/components/CameraComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/controller/LookOrientation.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/movement/JumpState.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/movement/MovementIntent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/movement/components/CharacterFacts.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/movement/components/CharacterMovement.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/movement/components/MotionChannels.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/movement/components/MovementSessions.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/movement/components/MovementTuning.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/net/NetParticipantIdentity.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/net/NetReplicationComponents.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/net/NetSpawnPrefab.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/physics/components/CharacterController.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/render/IrradianceVolumeComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/render/PointLightComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/render/SpotLightComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/render/StaticMeshComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/render/ZoneLightmapComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/render/skinned_mesh/SkinnedMeshComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/world/identity/PersistentIdComponent.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/world/scene/SceneInstance.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/world/transform/TransformComponents.h"
        "${SENCHA_ENGINE_INCLUDE_DIR}/zone/WorldConnectionComponents.h"
)

if(SENCHA_ENABLE_COOK)
    target_compile_definitions(sencha_engine PUBLIC SENCHA_ENABLE_COOK)

    # BC block encoders for the texture cook (docs/assets/pipeline.md,
    # Decisions E/L): bc7enc (BC7) + rgbcx (BC4/BC5) + bc7decomp (tests
    # decode-verify the encoder). MIT, single-purpose — the stb precedent.
    # Cook-only: never rides in a shipping binary.
    #
    # SOURCE_SUBDIR names a directory these repos do not have, so
    # FetchContent_MakeAvailable downloads without add_subdirectory: bc7enc's own
    # CMakeLists builds a test app we do not want, and the other two only need
    # their sources. This is the supported form -- calling FetchContent_Populate
    # directly is deprecated under CMP0169.
    #
    # bc7enc.c and mikktspace.c are plain C, hence the enable here.
    enable_language(C)
    include(FetchContent)
    FetchContent_Declare(
        bc7enc
        GIT_REPOSITORY https://github.com/richgel999/bc7enc.git
        GIT_TAG        f66c2e489b07138f2673a2fb3d27c1aa1d565c48
        SOURCE_SUBDIR  sencha-does-not-build-this
    )
    FetchContent_MakeAvailable(bc7enc)

    # glTF parsing for the mesh cook (Decision B): cgltf, single-header,
    # the stb precedent. The implementation TU lives in src/assets/cook.
    FetchContent_Declare(
        cgltf
        GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
        GIT_TAG        bbeb5b0b070ddacddac6852fb72143eb68454937 # v1.15
        SOURCE_SUBDIR  sencha-does-not-build-this
    )
    FetchContent_MakeAvailable(cgltf)

    # Tangent generation for the mesh cook (Decision M): MikkTSpace, the
    # de-facto standard. Cook-only, never ships — the glslang rule.
    FetchContent_Declare(
        mikktspace
        GIT_REPOSITORY https://github.com/mmikk/MikkTSpace.git
        GIT_TAG        3e895b49d05ea07e4c2133156cfa94369e19e409
        SOURCE_SUBDIR  sencha-does-not-build-this
    )
    FetchContent_MakeAvailable(mikktspace)

    # Vendored encoder/tangent sources compile in their own target so the
    # first-party diagnostic set never binds to third-party code. OBJECT rather
    # than STATIC for the sencha_imgui_support reason: the objects have to land
    # in the engine binary itself, and on Windows only the shared library's own
    # object files reach its export table.
    add_library(sencha_cook_thirdparty OBJECT
        "${bc7enc_SOURCE_DIR}/bc7enc.c"
        "${bc7enc_SOURCE_DIR}/bc7decomp.cpp"
        "${mikktspace_SOURCE_DIR}/mikktspace.c"
    )
    target_include_directories(sencha_cook_thirdparty SYSTEM PUBLIC
        "${bc7enc_SOURCE_DIR}"
        "${mikktspace_SOURCE_DIR}")
    target_link_libraries(sencha_engine PRIVATE sencha_cook_thirdparty)

    # SYSTEM: vendored third-party headers — keep their diagnostics out of the
    # engine's own. BUILD_INTERFACE only: cook headers are dev/editor-side; a
    # game module SDK does not include them, so these dirs stay out of the export.
    target_include_directories(sencha_engine SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${bc7enc_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${cgltf_SOURCE_DIR}>"
        "$<BUILD_INTERFACE:${mikktspace_SOURCE_DIR}>")
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
target_include_directories(sencha_engine SYSTEM PRIVATE "${stb_SOURCE_DIR}")

# Jolt physics: the runtime physics backend, compiled into the engine .so. Linked
# PRIVATE and firewalled behind the engine's physics facade (PIMPL), so no JPH
# header leaks through a public engine header and the FetchContent target stays
# out of the SDK export -- the glslang/VMA discipline above. Default float build:
# reproducible on one build, no CROSS_PLATFORM_DETERMINISTIC cost. The sample,
# viewer, and test apps are off (library only); OVERRIDE_CXX_FLAGS/ENABLE_ALL_WARNINGS
# off so Jolt does not clobber our flags or fail our build on its own warnings.
# Jolt defaults to the static MSVC runtime while everything else here uses the
# dynamic one, which links as an LNK2038 runtime-library mismatch.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS            OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD           OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST      OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES               OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER                OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS           OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS          OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION             OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.5.0
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)
target_link_libraries(sencha_engine PRIVATE Jolt)

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
    target_link_libraries(sencha_engine PRIVATE GPUOpen::VulkanMemoryAllocator)
    target_include_directories(sencha_engine SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${vulkanmemoryallocator_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

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
        # PRIVATE: glslang is compiled into the engine .so and only leaks through
        # public headers behind SENCHA_ENABLE_HOT_RELOAD, which a game module
        # never defines. Keeping it off the export interface lets install(EXPORT)
        # succeed without exporting the glslang FetchContent targets.
        target_link_libraries(sencha_engine
            PRIVATE
                glslang::glslang
                glslang::SPIRV
                glslang-default-resource-limits
        )
    endif()

    if(NOT IMGUI_DIR)
        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            # Docking-enabled, version-matched tag (adds DockSpace/DockBuilder; same
            # backends). Editor docks its panels into a default layout.
            GIT_TAG        v1.91.9-docking
        )
        FetchContent_MakeAvailable(imgui)
        set(IMGUI_DIR "${imgui_SOURCE_DIR}")
    endif()

    add_library(sencha_imgui_support OBJECT
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
    )
    add_library(sencha::imgui_support ALIAS sencha_imgui_support)

    # SYSTEM so imgui's headers do not report diagnostics into whichever
    # first-party target includes them.
    target_include_directories(sencha_imgui_support SYSTEM
        PUBLIC
            "${IMGUI_DIR}"
            "${IMGUI_DIR}/backends"
    )
    target_link_libraries(sencha_imgui_support
        PUBLIC
            SDL3::SDL3
            Vulkan::Vulkan
    )
    target_compile_features(sencha_imgui_support PUBLIC cxx_std_20)

    # The engine is the single ImGui provider: the editors link only
    # sencha::engine yet call ImGui, so the engine has to carry those symbols on
    # its exported surface. Hence an OBJECT library rather than a static one.
    # The Windows export table is generated from the shared library's own object
    # files, and symbols that arrive from a linked static library are not in it,
    # which leaves every editor with unresolved ImGui symbols. A consumer needs
    # the imgui headers (leaked by public debug/ and serialization headers) but
    # not the objects, so only the header dirs go on the public interface.
    target_link_libraries(sencha_engine PRIVATE sencha_imgui_support)
    target_include_directories(sencha_engine SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${IMGUI_DIR}>"
        "$<BUILD_INTERFACE:${IMGUI_DIR}/backends>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    # The generated export table resolves imported functions on its own, but a
    # consumer reaching imported DATA (GImGui, via the imgui_internal.h inline
    # helpers) must compile against a dllimport declaration or the reference
    # never targets the import table. ImGui anticipates DLL hosting with its
    # IMGUI_API macro on every function and on GImGui: export while compiling
    # into the engine, import everywhere else.
    if(WIN32)
        target_compile_definitions(sencha_imgui_support
            PRIVATE "IMGUI_API=__declspec(dllexport)")
        target_compile_definitions(sencha_engine
            PRIVATE   "IMGUI_API=__declspec(dllexport)"
            INTERFACE "IMGUI_API=__declspec(dllimport)")
        # binutils ld stops auto-exporting a DLL's symbols the moment any
        # symbol is explicitly dllexported, so without this the MinGW engine
        # would export ImGui and nothing else. MSVC has no equivalent problem:
        # its export table comes from the generated def file either way.
        if(NOT MSVC)
            target_link_options(sencha_engine PRIVATE "-Wl,--export-all-symbols")
        endif()
    endif()

    if(SENCHA_ENABLE_DEBUG_UI)
        target_compile_definitions(sencha_engine PUBLIC SENCHA_ENABLE_DEBUG_UI)
    endif()
endif()

if(SENCHA_ENABLE_RENDER_PROFILING)
    target_compile_definitions(sencha_engine PUBLIC SENCHA_ENABLE_RENDER_PROFILING)
endif()

# Build identity for render captures: a measurement that cannot name the build
# it came from cannot be compared against another one. Resolved at configure
# time, so a capture taken after later commits reports the configured SHA;
# reconfigure (or touch the cache) when that distinction matters.
execute_process(
    COMMAND git -C ${CMAKE_SOURCE_DIR} rev-parse --short HEAD
    OUTPUT_VARIABLE SENCHA_BUILD_SHA_VALUE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT SENCHA_BUILD_SHA_VALUE)
    set(SENCHA_BUILD_SHA_VALUE "unknown")
endif()
target_compile_definitions(sencha_engine PRIVATE
    SENCHA_BUILD_SHA="${SENCHA_BUILD_SHA_VALUE}"
    SENCHA_BUILD_TYPE="${CMAKE_BUILD_TYPE}")

add_library(engine ALIAS sencha_engine)

# --- Install / export the SDK (engine-roadmap.md: SDK boundary, shipped) ---
# A game developer consumes prebuilt binaries via find_package(Sencha): the
# engine .so, the full public header tree, the generated ABI/SDK headers, and
# the few vendored headers that leak through public engine headers. The app and
# editor executables install from their own subdirectories.
include(CMakePackageConfigHelpers)

# Export as sencha::engine (matching the in-tree ALIAS), not sencha::sencha_engine:
# ALIAS targets are not exported, so the consumer-facing name comes from
# EXPORT_NAME + the NAMESPACE below.
set_target_properties(sencha_engine PROPERTIES EXPORT_NAME engine)

install(TARGETS sencha_engine
    EXPORT SenchaTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# Public engine headers, the generated ABI fingerprint + SDK version headers,
# and the component companions the public headers include.
install(DIRECTORY "${SENCHA_ENGINE_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated-public/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated-components/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.sencha.h")

# Vendored headers re-exposed by public engine headers (Vulkan builds only).
if(SENCHA_ENABLE_VULKAN)
    install(FILES "${vulkanmemoryallocator_SOURCE_DIR}/include/vk_mem_alloc.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES "${IMGUI_DIR}/imgui.h" "${IMGUI_DIR}/imconfig.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()

# The exported target set: an out-of-tree project sees sencha::engine with the
# install-tree include dir and SDL3/Vulkan as found dependencies.
install(EXPORT SenchaTargets
    FILE SenchaTargets.cmake
    NAMESPACE sencha::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Sencha")

configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/SenchaConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SenchaConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Sencha")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SenchaConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SenchaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SenchaConfigVersion.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/SenchaGameModule.cmake"
    "${CMAKE_SOURCE_DIR}/cmake/SenchaComponentCodegen.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Sencha")
