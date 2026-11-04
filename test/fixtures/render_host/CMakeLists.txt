# The host the golden-image tests and the renderer A/B bench render in: a game
# module that places a camera and nothing else. It lives under test/ because
# tests are its only consumers -- it is a fixture, not an example to copy.
sencha_compile_shader(
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/golden_displace.frag.glsl"
    STAGE frag
    INCLUDE_DIRS "${SENCHA_SHADER_INCLUDE_DIR}"
    OUTPUT_SPV _golden_displace_spv
)
sencha_embed_spirv(
    SOURCE_SPV "${_golden_displace_spv}"
    VAR_NAME kGoldenDisplaceFragSpv
    OUT_HEADER _golden_displace_header
)

add_library(render_host MODULE RenderHostGame.cpp "${_golden_displace_header}")
target_link_libraries(render_host PRIVATE engine)
target_include_directories(render_host PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
sencha_warnings(render_host)
# OUTPUT_NAME "game": `app` discovers the module named game<ext> beside itself,
# so the harness runs a bare `<dir>/app +map levels/<name>`.
set_target_properties(render_host PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PREFIX ""
    OUTPUT_NAME "game")

# The runtime host beside the module, so the directory is self-contained.
add_dependencies(render_host app)
add_custom_command(TARGET render_host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:app> $<TARGET_FILE_DIR:render_host>/
    COMMENT "Bundling the Sencha runtime host (app) next to the render host module")
