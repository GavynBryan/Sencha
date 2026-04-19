add_executable(CubeDemo
    main.cpp
    CubeDemoGame.cpp
    CubeDemoScene.cpp
    CubeDemoSystems.cpp
    FreeCamera.cpp
)
target_link_libraries(CubeDemo PRIVATE engine)
