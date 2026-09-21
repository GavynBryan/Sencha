cmake_minimum_required(VERSION 3.20)

# A Sencha game module (game.so) and nothing else. Two front doors share one
# module definition: standalone against an installed SDK, or in-tree against
# the engine being built. See templates/fps/CMakeLists.txt for the long form.

set(SENCHA_GAME_MODULE_SOURCES
    src/ArenaComponents.cpp
    src/ArenaGame.cpp
    src/ArenaRuntimeComponents.cpp
    src/ArenaScore.cpp
    src/ArenaSessionPolicy.cpp
    src/ArenaSettingsData.cpp
    src/ArenaSteeringSystem.cpp
    src/LocalLookFollow.cpp
    src/PawnCameraSystem.cpp
    src/PawnSpawn.cpp)

# The game's components, declared by annotation like the engine's. Their
# metadata is generated into the build tree, and each header includes its
# companion by its path from this directory: src/ArenaStart.h includes
# <src/ArenaStart.sencha.h>.
set(SENCHA_GAME_COMPONENT_HEADERS
    src/ArenaScoreboard.h
    src/ArenaStart.h)

# The one sample. Remove this line and the directory to drop it.
include(samples/turret/TurretSample.cmake)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SenchaArenaTemplate LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    find_package(Sencha 0.1 REQUIRED)

    sencha_game_module(game
        SOURCES ${SENCHA_GAME_MODULE_SOURCES}
        INCLUDE_ROOT .
        COMPONENT_HEADERS ${SENCHA_GAME_COMPONENT_HEADERS})
    target_include_directories(game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
else()
    include("${CMAKE_SOURCE_DIR}/templates/InTreeTemplate.cmake")
    sencha_game_module(arena_game
        SOURCES ${SENCHA_GAME_MODULE_SOURCES}
        INCLUDE_ROOT .
        COMPONENT_HEADERS ${SENCHA_GAME_COMPONENT_HEADERS})
    target_include_directories(arena_game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    sencha_in_tree_template(arena_game)
endif()
