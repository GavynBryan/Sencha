set(SENCHA_2D_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SENCHA_2D_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE SENCHA_2D_SOURCES CONFIGURE_DEPENDS
    "${SENCHA_2D_SOURCE_DIR}/*.cpp"
)
file(GLOB_RECURSE SENCHA_2D_HEADERS CONFIGURE_DEPENDS
    "${SENCHA_2D_INCLUDE_DIR}/*.h"
)

if(NOT SENCHA_ENABLE_VULKAN)
    foreach(_pattern
        "[/\\\\]src[/\\\\]render[/\\\\]"
    )
        list(FILTER SENCHA_2D_SOURCES EXCLUDE REGEX "${_pattern}")
    endforeach()

    foreach(_pattern
        "[/\\\\]include[/\\\\]render[/\\\\]"
    )
        list(FILTER SENCHA_2D_HEADERS EXCLUDE REGEX "${_pattern}")
    endforeach()
endif()

add_library(sencha_2d STATIC
    ${SENCHA_2D_SOURCES}
    ${SENCHA_2D_HEADERS}
)
add_library(sencha::2d ALIAS sencha_2d)

target_include_directories(sencha_2d
    PUBLIC  "${SENCHA_2D_INCLUDE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(sencha_2d PUBLIC sencha_core)

if(SENCHA_ENABLE_VULKAN)
    include("${CMAKE_SOURCE_DIR}/cmake/SenchaShaders.cmake")

    set(_shader_inc "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include")

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

    sencha_embed_text(
        SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/internal/sprite.shader"
        VAR_NAME   kSpriteShaderMetadata
        OUT_HEADER _sprite_meta_h
    )

    add_custom_target(sencha_2d_embedded_shaders
        DEPENDS
            "${_sprite_vert_h}"
            "${_sprite_frag_h}"
            "${_sprite_meta_h}"
    )
    add_dependencies(sencha_2d sencha_2d_embedded_shaders)
endif()
