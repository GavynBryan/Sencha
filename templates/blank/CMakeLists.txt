cmake_minimum_required(VERSION 3.20)

# A Sencha game module (game.so) and nothing else. Two front doors share one
# module definition: standalone against an installed SDK, or in-tree against
# the engine being built. See templates/fps/CMakeLists.txt for the long form.

set(SENCHA_GAME_MODULE_SOURCES
    src/BlankGame.cpp)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SenchaBlankTemplate LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    find_package(Sencha 0.1 REQUIRED)

    sencha_game_module(game
        SOURCES ${SENCHA_GAME_MODULE_SOURCES}
        INCLUDE_ROOT src)
else()
    include("${CMAKE_SOURCE_DIR}/templates/InTreeTemplate.cmake")
    sencha_game_module(blank_game
        SOURCES ${SENCHA_GAME_MODULE_SOURCES}
        INCLUDE_ROOT src)
    sencha_in_tree_template(blank_game)
endif()
