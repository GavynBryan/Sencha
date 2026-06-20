# sencha_game_module(<target> [OUTPUT_NAME <name>] SOURCES <src>...)
#
# Builds a game module the Sencha host (app) and editor load at runtime: a
# MODULE library with hidden visibility (only SenchaCreateGameModule and the ABI
# descriptor are exported) linked against the prebuilt sencha::engine. This is
# the supported way to build a game.so out-of-tree; it mirrors the in-tree
# SceneViewer wiring so the published contract is the v4 module ABI alone.
#
# OUTPUT_NAME defaults to "game" so the host discovers the module beside itself
# (game<ext>) and a bundle runs with a bare `app +map ...`.
function(sencha_game_module target)
    cmake_parse_arguments(ARG "" "OUTPUT_NAME" "SOURCES" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "sencha_game_module(${target}): SOURCES is required")
    endif()
    if(NOT ARG_OUTPUT_NAME)
        set(ARG_OUTPUT_NAME "game")
    endif()

    add_library(${target} MODULE ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE sencha::engine)
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX ""
        OUTPUT_NAME "${ARG_OUTPUT_NAME}")
endfunction()
