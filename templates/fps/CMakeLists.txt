cmake_minimum_required(VERSION 3.20)

# A Sencha game module (game.so) and nothing else: no engine source. Two front
# doors share one module definition:
#
#  - Standalone (how a game ships): configured on its own against an installed
#    Sencha SDK. find_package(Sencha) provides the engine, the editor, and the
#    host. Nothing here refers to anything above this directory.
#        cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
#        cmake --build build
#
#  - In-tree (engine development): the repository adds this directory, so the
#    module links the engine being built and always carries its ABI fingerprint.
#    The .so lands in build/game.so under this directory, the path
#    project.senchaproj names, so the in-tree editor and host load it directly.

set(SENCHA_GAME_MODULE_SOURCES
    src/FpsComponents.cpp
    src/FpsGame.cpp
    src/FpsRuntimeComponents.cpp
    src/FpsSessionPolicy.cpp
    src/FpsSettingsData.cpp
    src/FpsSteeringSystem.cpp
    src/LocalLookFollow.cpp
    src/PawnCameraSystem.cpp
    src/PawnSpawn.cpp
    src/PawnStreaming.cpp)

# The game's components, declared by annotation like the engine's. Their
# metadata is generated into the build tree, and each header includes its
# companion by its own name: src/FpsStart.h includes <FpsStart.sencha.h>.
set(SENCHA_GAME_COMPONENT_HEADERS
    src/FpsStart.h)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SenchaFpsTemplate LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    # The version matches the engine SDK (SenchaConfigVersion.cmake); any 0.1.x
    # SDK satisfies it. find_package also brings in sencha_game_module().
    find_package(Sencha 0.1 REQUIRED)

    # OUTPUT_NAME defaults to "game", so the host discovers it beside itself.
    sencha_game_module(game
        SOURCES ${SENCHA_GAME_MODULE_SOURCES}
        INCLUDE_ROOT src
        COMPONENT_HEADERS ${SENCHA_GAME_COMPONENT_HEADERS})
else()
    include("${CMAKE_SOURCE_DIR}/templates/InTreeTemplate.cmake")
    sencha_game_module(fps_game
        SOURCES ${SENCHA_GAME_MODULE_SOURCES}
        INCLUDE_ROOT src
        COMPONENT_HEADERS ${SENCHA_GAME_COMPONENT_HEADERS})
    sencha_in_tree_template(fps_game)
endif()
