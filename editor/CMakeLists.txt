if(NOT SENCHA_ENABLE_VULKAN)
    message(STATUS "Skipping sencha_editor: requires SENCHA_ENABLE_VULKAN=ON")
    return()
endif()

file(GLOB_RECURSE SENCHA_EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE SENCHA_EDITOR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
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

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/editor_wideline.vert.glsl"
    STAGE      vert
    OUTPUT_SPV _editor_wideline_vert_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_editor_wideline_vert_spv}"
    VAR_NAME   kEditorWideLineVertSpv
    OUT_HEADER _editor_wideline_vert_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/editor_wideline.frag.glsl"
    STAGE      frag
    OUTPUT_SPV _editor_wideline_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_editor_wideline_frag_spv}"
    VAR_NAME   kEditorWideLineFragSpv
    OUT_HEADER _editor_wideline_frag_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert.glsl"
    STAGE      vert
    OUTPUT_SPV _fullscreen_vert_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_fullscreen_vert_spv}"
    VAR_NAME   kFullscreenVertSpv
    OUT_HEADER _fullscreen_vert_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_brightpass.frag.glsl"
    STAGE      frag
    OUTPUT_SPV _bloom_brightpass_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_bloom_brightpass_frag_spv}"
    VAR_NAME   kBloomBrightpassFragSpv
    OUT_HEADER _bloom_brightpass_frag_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_blur.frag.glsl"
    STAGE      frag
    OUTPUT_SPV _bloom_blur_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_bloom_blur_frag_spv}"
    VAR_NAME   kBloomBlurFragSpv
    OUT_HEADER _bloom_blur_frag_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_composite.frag.glsl"
    STAGE      frag
    OUTPUT_SPV _bloom_composite_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_bloom_composite_frag_spv}"
    VAR_NAME   kBloomCompositeFragSpv
    OUT_HEADER _bloom_composite_frag_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/viewport_backdrop.vert.glsl"
    STAGE      vert
    OUTPUT_SPV _viewport_backdrop_vert_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_viewport_backdrop_vert_spv}"
    VAR_NAME   kViewportBackdropVertSpv
    OUT_HEADER _viewport_backdrop_vert_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/viewport_backdrop.frag.glsl"
    STAGE      frag
    OUTPUT_SPV _viewport_backdrop_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_viewport_backdrop_frag_spv}"
    VAR_NAME   kViewportBackdropFragSpv
    OUT_HEADER _viewport_backdrop_frag_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/editor_solid.vert.glsl"
    STAGE      vert
    OUTPUT_SPV _editor_solid_vert_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_editor_solid_vert_spv}"
    VAR_NAME   kEditorSolidVertSpv
    OUT_HEADER _editor_solid_vert_header
)

sencha_compile_shader(
    SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/editor_solid.frag.glsl"
    STAGE      frag
    OUTPUT_SPV _editor_solid_frag_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_editor_solid_frag_spv}"
    VAR_NAME   kEditorSolidFragSpv
    OUT_HEADER _editor_solid_frag_header
)

set(SENCHA_EDITOR_GENERATED_HEADERS
    "${_grid_vert_header}"
    "${_grid_frag_header}"
    "${_editor_line_vert_header}"
    "${_editor_line_frag_header}"
    "${_editor_wideline_vert_header}"
    "${_editor_wideline_frag_header}"
    "${_fullscreen_vert_header}"
    "${_bloom_brightpass_frag_header}"
    "${_bloom_blur_frag_header}"
    "${_bloom_composite_frag_header}"
    "${_editor_solid_vert_header}"
    "${_editor_solid_frag_header}"
    "${_viewport_backdrop_vert_header}"
    "${_viewport_backdrop_frag_header}"
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
    # Dev-time editor asset root. Packaging will replace this with a resolved
    # install path; for now editor visuals (e.g. camera.glb) load from source.
    SENCHA_EDITOR_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/meshes"
    SENCHA_EDITOR_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts"
    SENCHA_EDITOR_SKIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/skin"
    SENCHA_EDITOR_THEME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/themes"
)

# Install the prebuilt editor into the SDK (real-engine-roadmap.md Phase 0). The
# editor still resolves its own visual assets from the source tree via the
# SENCHA_EDITOR_*_DIR defines above; making those relocatable is the Phase 3
# packaging concern. RPATH matches the app's bin/ + lib/ layout.
set_target_properties(sencha_editor PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS sencha_editor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
