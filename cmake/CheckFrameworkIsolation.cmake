# Enforces the D-J dependency rule for the gameplay framework:
#   - framework code may depend ONLY on core/, ecs/, math/, other framework/, the
#     physics DATA components (physics/components/, e.g. CharacterController: a
#     Jolt-free POD gameplay reads/writes directly), the standard library, and the
#     one serializer seam it needs (world/serialization/IComponentSerializer.h).
#     Everything else (render/, graphics/, audio/, world/ internals, scene codecs,
#     the physics SIMULATION (physics/ outside components/, PhysicsWorld,
#     CharacterMover, Jolt), editor/, assets/, app/, runtime/, zone/, ...) is
#     forbidden: gameplay stays decoupled from render/scene/backend. The physics
#     backend boundary is still hard (CheckPhysicsIsolation keeps Jolt behind
#     engine/src/physics/); framework touches physics data, never the simulation.
#   - engine code outside framework/ must NOT include framework/ (engine never
#     depends on gameplay).
#
# Direction 1 is a strict allowlist (default-deny), not a list of banned names.
# A blocklist only catches the leaks you thought to name; the moment a framework
# file reaches for world/ internals or physics/ the way to find out should be a
# red test, not a code review. Adding a new allowed dependency is a conscious edit
# to ALLOWED_PREFIXES below, on the record.
#
# Run standalone (no build/Vulkan needed):
#   cmake -P cmake/CheckFrameworkIsolation.cmake

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Include-path prefixes a framework/ file may pull. A bare stdlib header (no '/')
# is always allowed. Everything else is a violation.
set(ALLOWED_PREFIXES
    "core/"
    "ecs/"
    "math/"
    "framework/"
    "physics/components/"
)
# Specific non-prefix headers a framework file may include. Kept exact so the
# framework gets the serializer interface without the render/audio-pulling codecs.
set(ALLOWED_EXACT
    "world/serialization/IComponentSerializer.h"
)

set(VIOLATIONS "")

# 1. framework/ may depend only on the allowlist.
file(GLOB_RECURSE FRAMEWORK_FILES
    "${REPO}/engine/include/framework/*"
    "${REPO}/engine/src/framework/*"
)
foreach(file ${FRAMEWORK_FILES})
    file(READ "${file}" content)
    file(RELATIVE_PATH rel "${REPO}" "${file}")
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]" includes "${content}")
    foreach(directive ${includes})
        string(REGEX REPLACE "#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]" "\\1" path "${directive}")
        if(NOT path MATCHES "/")
            continue() # stdlib header
        endif()
        set(ok FALSE)
        foreach(prefix ${ALLOWED_PREFIXES})
            if(path MATCHES "^${prefix}")
                set(ok TRUE)
                break()
            endif()
        endforeach()
        if(NOT ok)
            foreach(exact ${ALLOWED_EXACT})
                if(path STREQUAL "${exact}")
                    set(ok TRUE)
                    break()
                endif()
            endforeach()
        endif()
        if(NOT ok)
            list(APPEND VIOLATIONS "${rel} includes '${path}' (framework may depend only on core/, ecs/, math/, framework/, physics/components/, and the serializer seam)")
        endif()
    endforeach()
endforeach()

# 2. engine code outside framework/ must not depend on framework/.
file(GLOB_RECURSE ENGINE_FILES
    "${REPO}/engine/include/*"
    "${REPO}/engine/src/*"
)
foreach(file ${ENGINE_FILES})
    if(file MATCHES "/framework/")
        continue()
    endif()
    file(READ "${file}" content)
    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]framework/" hit "${content}")
    if(hit)
        file(RELATIVE_PATH rel "${REPO}" "${file}")
        list(APPEND VIOLATIONS "${rel} includes 'framework/' (engine must not depend on the framework)")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "framework isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "framework isolation check failed: ${n} violation(s)")
endif()

list(LENGTH FRAMEWORK_FILES fw_count)
message(STATUS "framework isolation OK (${fw_count} framework files clean)")
