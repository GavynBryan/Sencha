# The default game module: a pure cooked-scene viewer (no gameplay). Built as a
# loadable MODULE with hidden visibility, so only SenchaCreateGameModule and the
# ABI descriptor are exported. The `app` host loads it (or any game module) at
# runtime; see app/main.cpp.
add_library(SceneViewer MODULE
    SceneViewerGame.cpp
    FreeCamera.cpp)
target_link_libraries(SceneViewer PRIVATE engine)
# OUTPUT_NAME "game": the bundle's module is the default one `app` discovers
# beside itself (game<ext>), so the bundle runs with a bare `app +map ...`. The
# descriptive name stays on the CMake target and the directory.
set_target_properties(SceneViewer PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    PREFIX ""
    OUTPUT_NAME "game")

# Bundle the generic runtime host next to the module, producing a self-contained
# runnable: <dir>/app + <dir>/game.so. This is the shipping layout (host exe +
# game module + content); a real game module would also stage its cooked content
# here. Run from the bundle dir: ./app +map levels/<name>
add_dependencies(SceneViewer app)
add_custom_command(TARGET SceneViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:app> $<TARGET_FILE_DIR:SceneViewer>/
    COMMENT "Bundling the Sencha runtime host (app) next to SceneViewer.so")
