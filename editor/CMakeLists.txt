if(NOT SENCHA_ENABLE_VULKAN)
    message(STATUS "Skipping sencha_editor: requires SENCHA_ENABLE_VULKAN=ON")
    return()
endif()

file(GLOB_RECURSE SENCHA_EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

file(GLOB_RECURSE SENCHA_EDITOR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.vert.glsl"
    STAGE      vert
    OUTPUT_SPV _grid_vert_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_grid_vert_spv}"
    VAR_NAME   kGridVertSpv
    OUT_HEADER _grid_vert_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/grid.frag.glsl"
    STAGE      frag
    OUTPUT_SPV _grid_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_grid_frag_spv}"
    VAR_NAME   kGridFragSpv
    OUT_HEADER _grid_frag_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/editor_line.vert.glsl"
    STAGE      vert
    OUTPUT_SPV _editor_line_vert_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_editor_line_vert_spv}"
    VAR_NAME   kEditorLineVertSpv
    OUT_HEADER _editor_line_vert_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/editor_line.frag.glsl"
    STAGE      frag
    OUTPUT_SPV _editor_line_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_editor_line_frag_spv}"
    VAR_NAME   kEditorLineFragSpv
    OUT_HEADER _editor_line_frag_header
)

set(SENCHA_EDITOR_GENERATED_HEADERS
    "${_grid_vert_header}"
    "${_grid_frag_header}"
    "${_editor_line_vert_header}"
    "${_editor_line_frag_header}"
)

add_executable(sencha_editor
    ${SENCHA_EDITOR_SOURCES}
    ${SENCHA_EDITOR_HEADERS}
    ${SENCHA_EDITOR_GENERATED_HEADERS}
)

target_link_libraries(sencha_editor PRIVATE sencha::engine)
target_include_directories(sencha_editor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(sencha_editor PRIVATE
    SENCHA_ENABLE_VULKAN
    SENCHA_ENABLE_DEBUG_UI
)
