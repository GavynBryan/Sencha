# Enforces the render-domain firewall: engine/{include,src}/render is render
# policy, and policy must be readable, testable, and debuggable without a
# Vulkan device in scope.
#
#   - No file under render/ may include <vulkan/...> or <vk_mem_alloc.h>.
#   - No file under render/ may name a Vulkan symbol (Vk*, vk*, VK_*).
#
# This inverts the physics/net firewalls. Those allowlist the *directory* that
# owns the backend; here the backend directory is graphics/vulkan/ and render/
# is the side that must stay clean, so the allowlist is a list of files that
# have not been migrated yet. It shrinks. It must never grow: a new render file
# that needs Vulkan is a file that belongs under graphics/vulkan/ instead.
#
# Scope note: matching is textual, so this catches *direct* includes only. A
# render header that reaches Vulkan through an innocently-named engine header
# will not trip it. The allowlist, not the regex, is what tracks those; measure
# the transitive closure with the compiler when shrinking the list:
#
#   echo '#include <render/Foo.h>' | g++ -std=c++20 -x c++ -Iengine/include -M - \
#       | tr ' \\' '\n\n' | grep -E 'vulkan/vulkan\.h|vk_mem_alloc\.h'
#
# Comments are stripped before matching. Prose that names VkFormat or
# VK_FORMAT_R8G8B8A8_UNORM to say what a CPU-side enum maps to is documentation
# doing its job, not a dependency.
#
# Run standalone (no build needed):
#   cmake -P cmake/CheckRenderIsolation.cmake

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Render files that still name Vulkan, with the phase that retires each.
#
# The passes and their lighting bindings are Vulkan types that currently sit in
# the wrong directory; they move under graphics/vulkan/ wholesale.
# RenderComponentRegistration and ZoneLightmapComponent are the two that need
# real work rather than a move.
#
# Do not add to this list. Shrink it.
set(RENDER_VULKAN_ALLOWED
    "/engine/include/render/LightBindings.h"          # -> graphics/vulkan/
    "/engine/include/render/MeshForwardPass.h"        # -> graphics/vulkan/
    "/engine/include/render/ShadowDepthPass.h"        # -> graphics/vulkan/
    "/engine/src/render/LightBindings.cpp"            # -> graphics/vulkan/
    "/engine/src/render/MeshForwardPass.cpp"          # -> graphics/vulkan/
    "/engine/src/render/ShadowDepthPass.cpp"          # -> graphics/vulkan/
    "/engine/src/render/ProbeVolumeSet.cpp"           # probe residency; owns image views
    "/engine/src/render/static_mesh/GpuStaticMesh.cpp" # buffer upload; the .h is already clean
)

set(VIOLATIONS "")

file(GLOB_RECURSE RENDER_FILES
    "${REPO}/engine/include/render/*"
    "${REPO}/engine/src/render/*"
)
foreach(file ${RENDER_FILES})
    set(allowed FALSE)
    foreach(ok ${RENDER_VULKAN_ALLOWED})
        if(file MATCHES "${ok}$")
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
    # prose mention of a Vulkan enum does not read as a dependency.
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" "" content "${content}")
    string(REGEX REPLACE "//[^\n]*" "" content "${content}")

    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"](vulkan/|vk_mem_alloc)"
        inc_hit "${content}")
    if(inc_hit)
        list(APPEND VIOLATIONS
            "${rel} includes a Vulkan header (render policy stays behind graphics/vulkan/)")
    endif()

    # No word-boundary anchor: CMake's regex engine does not implement \b, and
    # a pattern written with one silently matches nothing. These three prefixes
    # are distinctive enough on their own, and a Vulkan name embedded in a
    # longer identifier is still a dependency worth reporting.
    string(REGEX MATCH "(Vk[A-Z][A-Za-z0-9_]*|vk[A-Z][A-Za-z0-9_]*|VK_[A-Z0-9_]+)"
        type_hit "${content}")
    if(type_hit)
        list(APPEND VIOLATIONS
            "${rel} names a Vulkan symbol (${type_hit}); render policy must not depend on the backend")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "render isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "render isolation check failed: ${n} violation(s)")
endif()

list(LENGTH RENDER_VULKAN_ALLOWED allowed_count)
message(STATUS
    "render isolation OK (Vulkan confined to graphics/vulkan/; ${allowed_count} file(s) awaiting migration)")
