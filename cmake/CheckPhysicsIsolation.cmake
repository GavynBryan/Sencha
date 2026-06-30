# Enforces the physics backend firewall: Jolt is an implementation detail of the
# physics module and must not leak anywhere else.
#
#   - No engine file outside engine/src/physics/ may include a Jolt header
#     (<Jolt/...>) or name a Jolt type (JPH::). Public physics headers
#     (engine/include/physics/) are held to this too: the whole point of the
#     PIMPL firewall is that they expose mechanism-named types, never JPH.
#   - The one sanctioned exception is the collision-shape cook, which bakes Jolt
#     shapes. It is dev/cook-only (SENCHA_ENABLE_COOK), excluded from the runtime
#     build, so it cannot drag Jolt into a shipped runtime.
#
# Matches JPH:: (not JPH_) on purpose: JPH_* are Jolt config macros that appear
# legitimately in the allowed files; JPH:: is a type/namespace use, which is the
# leak we care about. A file that uses JPH:: types pulled in transitively through
# a non-Jolt-named header is exactly the re-exposure this catches.
#
# Run standalone (no build/Jolt needed):
#   cmake -P cmake/CheckPhysicsIsolation.cmake

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Files/dirs allowed to touch Jolt. Anything matching one of these path
# fragments is skipped. Adding a new Jolt dependency site is a conscious decision
# that belongs here, on the record.
set(PHYSICS_ALLOWED
    "/engine/src/physics/"
    "/engine/src/assets/cook/CollisionShapeCook.cpp"
)

set(VIOLATIONS "")

file(GLOB_RECURSE ENGINE_FILES
    "${REPO}/engine/include/*"
    "${REPO}/engine/src/*"
)
foreach(file ${ENGINE_FILES})
    set(allowed FALSE)
    foreach(ok ${PHYSICS_ALLOWED})
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

    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]Jolt/" inc_hit "${content}")
    if(inc_hit)
        list(APPEND VIOLATIONS "${rel} includes a <Jolt/...> header (Jolt must stay behind engine/src/physics/)")
    endif()

    string(REGEX MATCH "JPH::" jph_hit "${content}")
    if(jph_hit)
        list(APPEND VIOLATIONS "${rel} names a JPH:: type (Jolt must not leak past the physics PIMPL)")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "physics isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "physics isolation check failed: ${n} violation(s)")
endif()

list(LENGTH ENGINE_FILES engine_count)
message(STATUS "physics isolation OK (Jolt confined to engine/src/physics/ + the cook)")
