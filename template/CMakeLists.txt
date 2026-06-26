cmake_minimum_required(VERSION 3.20)
project(SenchaGameTemplate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This project builds ONLY a game module against a prebuilt Sencha SDK. No engine
# source is present or required. Point CMake at the installed SDK:
#
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
#   cmake --build build
#
# The version arg matches the engine SDK version (SenchaConfigVersion.cmake);
# SameMajorVersion compatibility means any 0.1.x SDK satisfies it.
find_package(Sencha 0.1 REQUIRED)

# sencha_game_module() (from the SDK) builds a MODULE with hidden visibility and
# the v4 ABI export wired in, linked to the prebuilt sencha::engine. OUTPUT_NAME
# defaults to "game", so the host discovers it beside the executable.
sencha_game_module(game
    SOURCES
        src/TemplateGame.cpp
        src/FreeCamera.cpp)
