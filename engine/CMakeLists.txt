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
file(GLOB SENCHA_ABI_FINGERPRINT_HEADERS CONFIGURE_DEPENDS
    "${SENCHA_ENGINE_INCLUDE_DIR}/app/*.h"
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

# --- Engine SDK version (real-engine-roadmap.md Phase 0) ---
# The semantic version of the published product, sourced from the root project()
# VERSION. A game module includes this to record which engine SDK it targets;
# the install's package version file (SenchaConfigVersion.cmake) uses the same
# numbers so find_package(Sencha <version>) can enforce compatibility. This is
# author-set and stable across rebuilds; it is NOT the ABI fingerprint above.
file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated-public/app/SenchaSdkVersion.h"
    CONTENT "#pragma once
// Generated by engine/CMakeLists.txt from the root project() VERSION -- do not
// edit. The engine SDK version; see real-engine-roadmap.md Phase 0.
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

# Diagnostics: PRIVATE so they apply to the engine build without leaking onto
# consumers. Not -Werror yet — the warning set still needs triage.
target_compile_options(sencha_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive->
)

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
    # SYSTEM: vendored third-party headers — keep their -Wall/-Wextra noise out
    # of the engine's own diagnostics.
    # BUILD_INTERFACE only: cook headers are dev/editor-side; a game module SDK
    # does not include them, so these vendored dirs stay out of the export.
    target_include_directories(sencha_engine SYSTEM PUBLIC "$<BUILD_INTERFACE:${bc7enc_SOURCE_DIR}>")

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
    target_include_directories(sencha_engine SYSTEM PUBLIC "$<BUILD_INTERFACE:${cgltf_SOURCE_DIR}>")

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
    target_include_directories(sencha_engine SYSTEM PUBLIC "$<BUILD_INTERFACE:${mikktspace_SOURCE_DIR}>")
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
    # PRIVATE link: the VMA implementation is compiled into the engine .so. Its
    # header leaks through public graphics/vulkan headers, so the header dir
    # (not the link) goes onto the public interface, mapped to the install tree
    # for an out-of-tree module. vk_mem_alloc.h itself is installed below.
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

    add_library(sencha_imgui_support STATIC
        "${IMGUI_DIR}/imgui.cpp"
        "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_tables.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
        "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
    )
    add_library(sencha::imgui_support ALIAS sencha_imgui_support)

    target_include_directories(sencha_imgui_support
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

    # PRIVATE link: imgui is compiled into sencha_imgui_support and statically
    # linked into the engine .so, which re-exports the ImGui symbols (the editor
    # links only sencha::engine yet calls ImGui). A consumer therefore needs the
    # imgui headers (leaked by public debug/ and serialization headers) but not
    # the static lib, so only the header dirs go on the public interface.
    target_link_libraries(sencha_engine PRIVATE sencha_imgui_support)
    target_include_directories(sencha_engine SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${IMGUI_DIR}>"
        "$<BUILD_INTERFACE:${IMGUI_DIR}/backends>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

    if(SENCHA_ENABLE_DEBUG_UI)
        target_compile_definitions(sencha_engine PUBLIC SENCHA_ENABLE_DEBUG_UI)
    endif()
endif()

add_library(engine ALIAS sencha_engine)

# --- Install / export the SDK (real-engine-roadmap.md Phase 0) ---
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

# Public engine headers and the generated ABI fingerprint + SDK version headers.
install(DIRECTORY "${SENCHA_ENGINE_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated-public/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

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
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Sencha")
