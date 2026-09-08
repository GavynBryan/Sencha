# The host the golden-image tests and the renderer A/B bench render in: a game
# module that places a camera and nothing else. It lives under test/ because
# tests are its only consumers -- it is a fixture, not an example to copy.
add_library(render_host MODULE RenderHostGame.cpp)
target_link_libraries(render_host PRIVATE engine)
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
