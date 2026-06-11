add_executable(CubeDemo
    main.cpp
    CubeDemoGame.cpp
    CubeDemoScene.cpp
    CubeDemoSystems.cpp
    FreeCamera.cpp
)
target_link_libraries(CubeDemo PRIVATE engine)

# Dev-asset generator: produces the demo's .smesh at build time so the demo
# exercises the file-based asset path end to end (docs/assets/pipeline.md,
# Stage 1). Authored assets (.smat, textures) are committed under assets/
# and copied next to the binary.
add_executable(GenerateCubeDemoAssets GenerateCubeDemoAssets.cpp)
target_link_libraries(GenerateCubeDemoAssets PRIVATE engine)

add_custom_command(TARGET CubeDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/cube_demo_scene.json
        $<TARGET_FILE_DIR:CubeDemo>/cube_demo_scene.json
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:CubeDemo>/assets
    COMMAND GenerateCubeDemoAssets
        $<TARGET_FILE_DIR:CubeDemo>/assets
        $<TARGET_FILE_DIR:CubeDemo>/cube_demo_scene.json
)
add_dependencies(CubeDemo GenerateCubeDemoAssets)
