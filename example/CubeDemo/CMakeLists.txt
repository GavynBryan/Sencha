add_executable(CubeDemo
    main.cpp
    CubeDemoGame.cpp
    CubeDemoScene.cpp
    CubeDemoSystems.cpp
    FreeCamera.cpp
)
target_link_libraries(CubeDemo PRIVATE engine)

add_custom_command(TARGET CubeDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/cube_demo_scene.json
        $<TARGET_FILE_DIR:CubeDemo>/cube_demo_scene.json
)
