file(GLOB_RECURSE SENCHA_3D_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE SENCHA_3D_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

include("${CMAKE_SOURCE_DIR}/cmake/SenchaShaders.cmake")

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

add_library(sencha_3d STATIC
    ${SENCHA_3D_SOURCES}
    ${SENCHA_3D_HEADERS}
    "${_mesh_forward_vert_header}"
    "${_mesh_forward_frag_header}"
)
add_library(sencha::3d ALIAS sencha_3d)

target_include_directories(sencha_3d
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(sencha_3d PUBLIC sencha_core)
