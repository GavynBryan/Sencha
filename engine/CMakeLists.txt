option(SENCHA_ENABLE_VULKAN
    "Build Sencha Vulkan rendering backend"
    ON)

option(SENCHA_ENABLE_HOT_RELOAD
    "Link glslang into the engine for runtime GLSL compilation and live shader \
reload.  Always OFF in release builds -- shipping binaries must not contain a \
GLSL compiler."
    OFF)

# ── Source discovery ──────────────────────────────────────────────────────────

set(SENCHA_ENGINE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SENCHA_ENGINE_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE SENCHA_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${SENCHA_ENGINE_SOURCE_DIR}/*.cpp"
)
file(GLOB_RECURSE SENCHA_ENGINE_HEADERS CONFIGURE_DEPENDS
    "${SENCHA_ENGINE_INCLUDE_DIR}/*.h"
)

if(NOT SENCHA_ENABLE_VULKAN)
    list(FILTER SENCHA_ENGINE_SOURCES EXCLUDE
        REGEX "[/\\\\]src[/\\\\]render[/\\\\]backend[/\\\\]vulkan[/\\\\]")
    list(FILTER SENCHA_ENGINE_HEADERS EXCLUDE
        REGEX "[/\\\\]include[/\\\\]render[/\\\\]backend[/\\\\]vulkan[/\\\\]")
endif()

# ── Library target ────────────────────────────────────────────────────────────

find_package(SDL3 REQUIRED CONFIG)

add_library(sencha_engine STATIC
    ${SENCHA_ENGINE_SOURCES}
    ${SENCHA_ENGINE_HEADERS}
)
add_library(sencha::engine ALIAS sencha_engine)

target_include_directories(sencha_engine
    PUBLIC  "${SENCHA_ENGINE_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"   # <shaders/kFoo.h>
)
target_link_libraries(sencha_engine PUBLIC SDL3::SDL3)

# ── stb_image ─────────────────────────────────────────────────────────────────

include(FetchContent)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        ae721c50eaf761660b4f90cc590453cdb0c2acd0 # stb_image 2.30
)
FetchContent_MakeAvailable(stb)
target_include_directories(sencha_engine PRIVATE "${stb_SOURCE_DIR}")

# ── Vulkan backend ────────────────────────────────────────────────────────────

if(SENCHA_ENABLE_VULKAN)

    find_package(Vulkan REQUIRED)
    target_link_libraries(sencha_engine PUBLIC Vulkan::Vulkan)

    # ── Offline shader build pipeline ─────────────────────────────────────
    #
    # All engine-internal (tier-1 bootstrap) shaders are compiled to SPIR-V by
    # glslc at *build* time and then embedded as uint32_t[] arrays in generated
    # headers.  SpriteFeature::Setup() calls CreateModuleFromSpirv() on those
    # arrays directly -- no file I/O, no runtime compiler.
    #
    # Generated headers land in:
    #   ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/
    # and are included as:
    #   #include <shaders/kFoo.h>
    # because ${CMAKE_CURRENT_BINARY_DIR}/generated is on the private include path.
    #
    # Adding a new engine shader:
    #   1. Drop source in engine/shaders/internal/
    #   2. Add a sprite.shader-style .shader metadata file
    #   3. Call sencha_compile_shader + sencha_embed_spirv + sencha_embed_text
    #      below and add the outputs to sencha_embedded_shaders DEPENDS.
    #   4. Include <shaders/kMyShaderSpv.h> in the feature .cpp.

    include("${CMAKE_SOURCE_DIR}/cmake/SenchaShaders.cmake")

    set(_shader_inc "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include")

    # sprite.vert ─────────────────────────────────────────────────────────
    sencha_compile_shader(
        SOURCE       "${CMAKE_CURRENT_SOURCE_DIR}/shaders/internal/sprite.vert.glsl"
        STAGE        vert
        INCLUDE_DIRS "${_shader_inc}"
        OUTPUT_SPV   _sprite_vert_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV  "${_sprite_vert_spv}"
        VAR_NAME    kSpriteVertSpv
        OUT_HEADER  _sprite_vert_h
    )

    # sprite.frag ─────────────────────────────────────────────────────────
    sencha_compile_shader(
        SOURCE       "${CMAKE_CURRENT_SOURCE_DIR}/shaders/internal/sprite.frag.glsl"
        STAGE        frag
        INCLUDE_DIRS "${_shader_inc}"
        OUTPUT_SPV   _sprite_frag_spv
    )
    sencha_embed_spirv(
        SOURCE_SPV  "${_sprite_frag_spv}"
        VAR_NAME    kSpriteFragSpv
        OUT_HEADER  _sprite_frag_h
    )

    # sprite.shader (pipeline metadata) ───────────────────────────────────
    sencha_embed_text(
        SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/internal/sprite.shader"
        VAR_NAME   kSpriteShaderMetadata
        OUT_HEADER _sprite_meta_h
    )

    # All generated shader headers must exist before any .cpp that includes
    # them is compiled.  The custom target creates that ordering relationship.
    add_custom_target(sencha_embedded_shaders
        DEPENDS
            "${_sprite_vert_h}"
            "${_sprite_frag_h}"
            "${_sprite_meta_h}"
    )
    add_dependencies(sencha_engine sencha_embedded_shaders)

    # ── VulkanMemoryAllocator ─────────────────────────────────────────────

    set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(VMA_BUILD_SAMPLES       OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.2.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    target_link_libraries(sencha_engine PUBLIC GPUOpen::VulkanMemoryAllocator)

    # ── glslang (hot-reload builds only) ──────────────────────────────────
    #
    # In production (SENCHA_ENABLE_HOT_RELOAD=OFF) glslang is never fetched,
    # never linked, and the GLSL compilation code paths in VulkanShaderCache
    # are compiled out via #ifdef SENCHA_ENABLE_HOT_RELOAD.
    #
    # Enable only for developer machines that need live shader editing.

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

endif() # SENCHA_ENABLE_VULKAN

# ── Compatibility alias ───────────────────────────────────────────────────────
# Apps and examples that already link the bare `engine` target continue to work.
add_library(engine INTERFACE)
target_link_libraries(engine INTERFACE sencha_engine)
