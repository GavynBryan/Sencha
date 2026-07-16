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
    # Vulkan is OFF, so listing them here too would be dead duplication.
    foreach(_pattern
        "[/\\]src[/\\]graphics[/\\]vulkan[/\\]"
        "[/\\]include[/\\]graphics[/\\]vulkan[/\\]"
        "[/\\]src[/\\]debug[/\\]ConsolePanel\\.cpp$"
        "[/\\]src[/\\]debug[/\\]ConsoleView\\.cpp$"
        "[/\\]src[/\\]debug[/\\]ImGuiDebugOverlay\\.cpp$"
        "[/\\]src[/\\]debug[/\\]TimingPanel\\.cpp$"
        "[/\\]include[/\\]debug[/\\]ConsolePanel\\.h$"
        "[/\\]include[/\\]debug[/\\]ConsoleView\\.h$"
        "[/\\]include[/\\]debug[/\\]IDebugPanel\\.h$"
        "[/\\]include[/\\]debug[/\\]ImGuiDebugOverlay\\.h$"
        "[/\\]include[/\\]debug[/\\]TimingPanel\\.h$"
    )
        list(FILTER SENCHA_ENGINE_SOURCES EXCLUDE REGEX "${_pattern}")
        list(FILTER SENCHA_ENGINE_HEADERS EXCLUDE REGEX "${_pattern}")
    endforeach()
endif()

if(NOT SENCHA_ENABLE_COOK)
    # Hot reload re-cooks on source change, so it is dev-only too and is
    # filtered alongside the cook layer it depends on.
    foreach(_pattern
        "[/\\]src[/\\]assets[/\\]cook[/\\]"
        "[/\\]include[/\\]assets[/\\]cook[/\\]"
        "[/\\]src[/\\]assets[/\\]hotreload[/\\]"
        "[/\\]include[/\\]assets[/\\]hotreload[/\\]"
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
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
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
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        OUTPUT_SPV _mesh_forward_frag_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_mesh_forward_frag_spv}"
        VAR_NAME kMeshForwardFragSpv
        OUT_HEADER _mesh_forward_frag_header
    )
    sencha_compile_shader(
        SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh_unlit.frag.glsl"
        STAGE frag
        INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        OUTPUT_SPV _mesh_unlit_frag_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_mesh_unlit_frag_spv}"
        VAR_NAME kMeshUnlitFragSpv
        OUT_HEADER _mesh_unlit_frag_header
    )
    list(APPEND SENCHA_ENGINE_GENERATED_HEADERS
        "${_mesh_forward_vert_header}"
        "${_mesh_forward_frag_header}"
        "${_mesh_unlit_frag_header}"
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
# does not force every game module to rebuild). Any token change still does.
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
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " _abi_hdr_text "${_abi_hdr_text}")
    string(REGEX REPLACE "//[^\n]*" "" _abi_hdr_text "${_abi_hdr_text}")
    string(REGEX REPLACE "[ \t\r\n]+" " " _abi_hdr_text "${_abi_hdr_text}")
    string(STRIP "${_abi_hdr_text}" _abi_hdr_text)
    string(SHA256 _abi_hdr_hash "${_abi_hdr_text}")
    string(APPEND _sencha_abi_accum "${_abi_hdr_hash}")
endforeach()
string(SHA256 _sencha_abi_full "${_sencha_abi_accum}")
string(SUBSTRING "${_sencha_abi_full}" 0 16 _sencha_abi_fp)
set(SENCHA_GAME_ABI_FINGERPRINT "0x${_sencha_abi_fp}ULL")
file(CONFIGURE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated-public/app/GameModuleAbiFingerprint.h"
    CONTENT "#pragma once\n#define SENCHA_GAME_MODULE_ABI_FINGERPRINT ${SENCHA_GAME_ABI_FINGERPRINT}\n"
    @ONLY
)

add_library(sencha_engine STATIC
    ${SENCHA_ENGINE_SOURCES}
    ${SENCHA_ENGINE_HEADERS}
    ${SENCHA_ENGINE_GENERATED_HEADERS}
)

add_library(Sencha::Engine ALIAS sencha_engine)

set_target_properties(sencha_engine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_include_directories(sencha_engine
    PUBLIC
        "$<BUILD_INTERFACE:${SENCHA_ENGINE_INCLUDE_DIR}>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated-public>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

if(SENCHA_ENABLE_VULKAN)
    target_link_libraries(sencha_engine PUBLIC Vulkan::Vulkan)
endif()

target_link_libraries(sencha_engine PUBLIC SDL3::SDL3)

if(SENCHA_ENABLE_DEBUG_UI)
    target_link_libraries(sencha_engine PUBLIC imgui)
endif()

if(SENCHA_ENABLE_PHYSICS)
    target_link_libraries(sencha_engine PUBLIC Jolt)
endif()

if(MSVC)
    target_compile_options(sencha_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(sencha_engine PRIVATE -Wall -Wextra)
endif()
