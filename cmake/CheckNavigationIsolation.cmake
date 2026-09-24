# Enforces the navigation backend firewall: Recast and Detour are implementation
# details of the navigation module and must not leak anywhere else.
#
#   - No engine file outside engine/src/navigation/ may include a Recast or
#     Detour header, or name one of their core types. Public navigation headers
#     (engine/include/navigation/) are held to this too: they expose
#     mechanism-named Sencha types, never dtPolyRef or rcConfig.
#   - Cook code reaches the tile builder through the backend-neutral signature
#     in navigation/NavTileBuild.h, so there is no cook exception.
#
# Run standalone (no build needed):
#   cmake -P cmake/CheckNavigationIsolation.cmake

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(NAVIGATION_ALLOWED
    "/engine/src/navigation/"
)

set(VIOLATIONS "")

file(GLOB_RECURSE ENGINE_FILES
    "${REPO}/engine/include/*"
    "${REPO}/engine/src/*"
)
foreach(file ${ENGINE_FILES})
    set(allowed FALSE)
    foreach(ok ${NAVIGATION_ALLOWED})
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

    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"](Recast|Detour)[A-Za-z]*\\.h" inc_hit "${content}")
    if(inc_hit)
        list(APPEND VIOLATIONS "${rel} includes a Recast/Detour header (they must stay behind engine/src/navigation/)")
    endif()

    string(REGEX MATCH "(dtNavMesh|dtPolyRef|dtMeshTile|dtQueryFilter|dtStatus|rcContext|rcConfig|rcHeightfield|rcPolyMesh)" type_hit "${content}")
    if(type_hit)
        list(APPEND VIOLATIONS "${rel} names a Recast/Detour type (${type_hit}) outside the navigation module")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "navigation isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "navigation isolation check failed: ${n} violation(s)")
endif()

message(STATUS "navigation isolation OK (Recast/Detour confined to engine/src/navigation/)")
