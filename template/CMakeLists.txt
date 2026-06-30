cmake_minimum_required(VERSION 3.20)

# This project builds ONLY a game module (game.so) — no engine source. It has two
# front doors that share one module definition (the sources below):
#
#  - Standalone (the supported way to ship a game): configured on its own against
#    a prebuilt Sencha SDK. find_package(Sencha) provides the engine, editor, and
#    host. This is what a game developer who is NOT touching engine source uses.
#        cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/sencha-sdk
#        cmake --build build
#
#  - In-tree (engine development): the repo root adds this via add_subdirectory,
#    so the module links the in-tree engine target and rebuilds WITH it in one
#    build. The host and the module then always carry the same ABI fingerprint —
#    no SDK install/rebuild dance while you are also changing the engine. The .so
#    is written to template/build/game.so, the path project.senchaproj already
#    points at, so the in-tree editor/host loads it with no extra steps.

set(SENCHA_TEMPLATE_MODULE_SOURCES
    src/TemplateGame.cpp)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone against a prebuilt SDK.
    project(SenchaGameTemplate LANGUAGES CXX)

    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)

    # The version arg matches the engine SDK version (SenchaConfigVersion.cmake);
    # SameMajorVersion compatibility means any 0.1.x SDK satisfies it. find_package
    # also brings in sencha_game_module().
    find_package(Sencha 0.1 REQUIRED)

    # OUTPUT_NAME defaults to "game", so the host discovers it beside the executable.
    sencha_game_module(game
        SOURCES ${SENCHA_TEMPLATE_MODULE_SOURCES})
else()
    # In-tree against the engine being built. Same helper the SDK ships, resolved
    # from the source tree; sencha::engine is the in-tree alias. Output goes to
    # template/build/game.so so the existing project.senchaproj resolves it.
    include("${CMAKE_SOURCE_DIR}/cmake/SenchaGameModule.cmake")

    sencha_game_module(template_game
        SOURCES ${SENCHA_TEMPLATE_MODULE_SOURCES})
    set_target_properties(template_game PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
endif()
