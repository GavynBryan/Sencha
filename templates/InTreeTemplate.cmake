# In-tree only: what a template's module gets when it is built as part of this
# repository rather than against an installed SDK. Included by each template's
# CMakeLists.txt from its in-tree branch; never installed, never referenced by
# the standalone branch.
include("${CMAKE_SOURCE_DIR}/cmake/SenchaGameModule.cmake")

function(sencha_in_tree_template target)
    # Held to the engine's own warning set. The SDK does not install
    # SenchaWarnings.cmake, so a standalone build keeps whatever diagnostics the
    # game project chose.
    sencha_warnings(${target})
    # Only the primary build tree owns <template>/build/game.so, the path the
    # template's project.senchaproj resolves. Every configure of this repository
    # builds a target by the same name, so without this the last one to link
    # decides which module the editor and the two-process tests load -- and a
    # sanitizer build writes one an uninstrumented host cannot load at all. A
    # second tree keeps its module beside its own binaries.
    if(CMAKE_BINARY_DIR STREQUAL "${CMAKE_SOURCE_DIR}/build")
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")
    endif()
endfunction()
