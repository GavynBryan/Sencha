# sencha_game_module(<target> [OUTPUT_NAME <name>] SOURCES <src>...
#                    [INCLUDE_ROOT <dir>] [COMPONENT_HEADERS <header>...])
#
# Builds a game module the Sencha host (app) and editor load at runtime: a
# MODULE library with hidden visibility (only SenchaCreateGameModule and the ABI
# descriptor are exported) linked against the prebuilt sencha::engine. This is
# the supported way to build a game.so out-of-tree; it mirrors the in-tree
# SceneViewer wiring so the published contract is the v4 module ABI alone.
#
# OUTPUT_NAME defaults to "game" so the host discovers the module beside itself
# (game<ext>) and a bundle runs with a bare `app +map ...`.
#
# COMPONENT_HEADERS lists the module's annotated component headers. Their
# ComponentDefinition companions are generated into the module's own build tree
# by the generator the SDK ships prebuilt, so declaring a game component costs
# the same as declaring an engine one and needs no Clang installed. A header's
# companion is included by the header's own path relative to INCLUDE_ROOT
# (the project directory unless given), with .sencha.h in place of .h. The
# module's components are validated against the engine's, so one that claims
# an engine identity, schema name or scene chunk fails this build naming the
# engine declaration.
function(sencha_game_module target)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME;INCLUDE_ROOT" "SOURCES;COMPONENT_HEADERS" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "sencha_game_module(${target}): SOURCES is required")
    endif()
    if(NOT ARG_OUTPUT_NAME)
        set(ARG_OUTPUT_NAME "game")
    endif()
    if(NOT ARG_INCLUDE_ROOT)
        set(ARG_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    add_library(${target} MODULE ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE sencha::engine)
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        OUTPUT_NAME "${ARG_OUTPUT_NAME}")

    if(ARG_COMPONENT_HEADERS)
        sencha_generate_component_metadata(${target}
            INCLUDE_ROOT "${ARG_INCLUDE_ROOT}"
            BASE sencha::engine
            HEADERS ${ARG_COMPONENT_HEADERS})
    endif()
endfunction()
