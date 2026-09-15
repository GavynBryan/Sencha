# Enforces the RmlUi firewall: RmlUi is an implementation detail of the retained
# authored UI runtime and must not leak anywhere else.
#
#   - No first-party file outside engine/src/ui/ may include an RmlUi header
#     (<RmlUi/...>) or name an RmlUi type (Rml::).
#
#     That directory -- not the rml/ subdirectory inside it -- is the line. The
#     runtime IS the document-engine integration: its contexts and documents are
#     RmlUi objects, and hiding them behind an adapter would be an interface
#     around one class with no boundary behind it. rml/ groups the interface
#     implementations because they are a family, not because it is a firewall.
#   - engine/include/ is held to this absolutely. The whole public header tree is
#     installed verbatim (engine/CMakeLists.txt install(DIRECTORY ...)), so an
#     RmlUi include in a public header would oblige the SDK to ship RmlUi's
#     headers too -- the position imgui.h is already in, and the one this guard
#     exists to avoid repeating.
#   - Editors are in scope, not just the engine. Kyusu is the heaviest intended
#     consumer of authored UI, which makes it the most likely place for a
#     shortcut straight to the DOM. It talks to ui/UiService.h like any other
#     host.
#
# Matches Rml:: (not Rml_ or RmlUi as a bare word) on purpose: prose naming the
# library, a CMake variable, and a directory called rml/ are all legitimate;
# Rml:: is a type or namespace use, which is the leak. Comments are stripped
# before matching, so a header explaining what it adapts stays legal.
#
# Run standalone (no build, no RmlUi needed):
#   cmake -P cmake/CheckUiIsolation.cmake

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# The one place RmlUi may be named. Adding a second entry is a claim that the
# boundary moved, which is a design review, not a checkbox.
set(UI_ALLOWED
    "/engine/src/ui/"
)

set(VIOLATIONS "")

file(GLOB_RECURSE FIRST_PARTY_FILES
    "${REPO}/engine/include/*.h"
    "${REPO}/engine/src/*.h"
    "${REPO}/engine/src/*.cpp"
    "${REPO}/editor/*.h"
    "${REPO}/editor/*.cpp"
    "${REPO}/app/*.h"
    "${REPO}/app/*.cpp"
    "${REPO}/example/*.h"
    "${REPO}/example/*.cpp"
    "${REPO}/templates/*.h"
    "${REPO}/templates/*.cpp"
    "${REPO}/test/*.h"
    "${REPO}/test/*.cpp"
)
foreach(file ${FIRST_PARTY_FILES})
    # A template's build/ directory holds a copy of nothing we own, but it is
    # cheap to be sure the glob never reads generated or fetched trees.
    if(file MATCHES "/build[^/]*/" OR file MATCHES "/_deps/")
        continue()
    endif()

    set(allowed FALSE)
    foreach(ok ${UI_ALLOWED})
        if(file MATCHES "${ok}")
            set(allowed TRUE)
            break()
        endif()
    endforeach()
    if(allowed)
        continue()
    endif()

    file(READ "${file}" content)
    file(RELATIVE_PATH rel "${REPO}" "${file}")

    # Block comments first, then line comments, so a commented-out include or a
    # header explaining the boundary does not read as a dependency.
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" "" content "${content}")
    string(REGEX REPLACE "//[^\n]*" "" content "${content}")

    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]RmlUi/" inc_hit "${content}")
    if(inc_hit)
        list(APPEND VIOLATIONS
            "${rel} includes an <RmlUi/...> header (RmlUi stays behind engine/src/ui/)")
    endif()

    string(REGEX MATCH "Rml::" rml_hit "${content}")
    if(rml_hit)
        list(APPEND VIOLATIONS
            "${rel} names an Rml:: type (the public surface is ui/UiService.h, not the DOM)")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "ui isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "ui isolation check failed: ${n} violation(s)")
endif()

message(STATUS "ui isolation OK (RmlUi confined to engine/src/ui/)")
