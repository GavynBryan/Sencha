cmake_minimum_required(VERSION 3.20)

# A Sencha game module (game.so) and nothing else. Two front doors share one
# module definition: standalone against an installed SDK, or in-tree against
# the engine being built. See templates/fps/CMakeLists.txt for the long form.

set(SENCHA_GAME_MODULE_SOURCES
    src/CameraRelativeSteeringSystem.cpp
    src/OrbitCameraSystem.cpp
    src/PawnSpawn.cpp
    src/PlatformerComponents.cpp
    src/PlatformerGame.cpp
    src/PlatformerRuntimeComponents.cpp
    src/PlatformerSessionPolicy.cpp
    src/PlatformerSettingsData.cpp)

set(SENCHA_GAME_COMPONENT_HEADERS
    src/PlatformerStart.h)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SenchaPlatformerTemplate LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    find_package(Sencha 0.1 REQUIRED)

    sencha_game_module(game
        SOURCES ${SENCHA_GAME_MODULE_SOURCES}
        INCLUDE_ROOT src
        COMPONENT_HEADERS ${SENCHA_GAME_COMPONENT_HEADERS})
else()
    include("${CMAKE_SOURCE_DIR}/templates/InTreeTemplate.cmake")
    sencha_game_module(platformer_game
        SOURCES ${SENCHA_GAME_MODULE_SOURCES}
        INCLUDE_ROOT src
        COMPONENT_HEADERS ${SENCHA_GAME_COMPONENT_HEADERS})
    sencha_in_tree_template(platformer_game)
endif()
