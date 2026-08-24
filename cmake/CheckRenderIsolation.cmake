# Enforces the render-domain firewall: engine/{include,src}/render is render
# policy, and policy must be readable, testable, and debuggable without a
# Vulkan device in scope.
#
#   - No file under render/ may include <vulkan/...> or <vk_mem_alloc.h>.
#   - No file under render/ may name a Vulkan symbol (Vk*, vk*, VK_*).
#
# This inverts the physics/net firewalls. Those allowlist the *directory* that
# owns the backend; here the backend directory is graphics/vulkan/ and render/
# is the side that must stay clean, so the allowlist is files rather than a
# directory. It must never grow.
#
# It does not follow that every allowlisted file is waiting to move. The two
# halves are recorded in docs/renderer/architecture.md: graphics/vulkan/ "knows
# nothing about ECS, zones, components, or gameplay", and the bridge types that
# record commands from render-domain state "live in render/". A recording pass
# takes RenderQueue, MaterialCache, and StaticMeshCache by reference, so moving
# one into the backend would invert the dependency it is there to keep pointing
# downward. Those files name Vulkan permanently and correctly.
#
# So the list is in two parts. The recording set is closed: it is what a device
# is genuinely required to test, and it is the population P4's pass mechanisms
# are meant to shrink by absorbing the duplicated preambles, not by relocating
# the passes. Everything else in render/ is policy and must stay clean.
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

# The recording set: bridge types that turn render-domain state into commands.
# Closed by design. A new entry here is a claim that the renderer grew a fourth
# recording surface, which is a design review, not a checkbox.
set(RENDER_RECORDING_SET
    "/engine/include/render/pass/LightBindings.h"           # descriptor set 2, shadow pool images
    "/engine/include/render/pass/MeshForwardPass.h"         # forward draw recording
    "/engine/include/render/pass/ShadowDepthPass.h"         # shadow view recording
    "/engine/src/render/pass/LightBindings.cpp"
    "/engine/src/render/pass/MeshForwardPass.cpp"
    "/engine/src/render/pass/ShadowDepthPass.cpp"
)

# Policy files that reach the backend directly and should not. Shrink this.
set(RENDER_VULKAN_PENDING
    # Creates and uploads the SH channel images inline. The residency policy is
    # split out into ProbeVolumeSlotTable; what remains here is image creation,
    # which wants an upload seam rather than a move.
    "/engine/src/render/ProbeVolumeSet.cpp"
    # Buffer upload for mesh geometry; the header is already clean.
    "/engine/src/render/static_mesh/GpuStaticMesh.cpp"
)

set(RENDER_VULKAN_ALLOWED ${RENDER_RECORDING_SET} ${RENDER_VULKAN_PENDING})

# A render *header* may name one backend header: graphics/vulkan/Renderer.h,
# which architecture.md calls "the one header both halves include" -- it
# declares IRenderFeature, RendererServices, and FrameContext, so a bridge type
# cannot be declared without it. Anything else from graphics/vulkan/ in a header
# propagates the backend to every consumer of that header, which is how
# ZoneLightmapComponent.h came to hand a Vulkan texture cache to scene
# serialization and schema startup.
#
# Implementation files are deliberately unrestricted here. render/ is defined as
# knowing Vulkan through the services it is handed, so a .cpp calling
# VulkanBufferService is the design working, not a leak.
set(RENDER_HEADER_BACKEND_ALLOWED
    "/engine/include/render/feature/ShadowRenderFeature.h"  # bridge type; Renderer.h only
    # Holds a graphics/vulkan/SkyGradientPass by value. That pass takes a matrix
    # and two colours and names no render-domain type, so it is backend rather
    # than a recording bridge -- which is why the recording set above did not
    # grow. What is left in render/ is the feature that decides where the two
    # colours come from, and it propagates the pass header to its consumers.
    "/engine/include/render/feature/SkyRenderFeature.h"
    # Holds the pre-skin compute pass by value, the SkyRenderFeature shape
    # again: the pass takes plain data (buffers, counts, offsets), so the
    # dispatch and its barrier live in the backend and the recording set did
    # not grow. What remains in render/ is the policy -- which instances,
    # which palettes, and the posed buffers' lifecycle.
    "/engine/include/render/feature/SkinnedPoseRenderFeature.h"
    ${RENDER_RECORDING_SET}
)

set(VIOLATIONS "")

file(GLOB_RECURSE RENDER_FILES
    "${REPO}/engine/include/render/*"
    "${REPO}/engine/src/render/*"
)
foreach(file ${RENDER_FILES})
    file(READ "${file}" content)
    file(RELATIVE_PATH rel "${REPO}" "${file}")

    # Block comments first, then line comments, so a commented-out include or a
    # prose mention of a Vulkan enum does not read as a dependency.
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" "" content "${content}")
    string(REGEX REPLACE "//[^\n]*" "" content "${content}")

    # Backend headers reached from a render header, checked before the Vulkan
    # allowlist because the recording set is exempt from that but still bounded
    # here: this rule is about what a header propagates, not about who may name
    # a Vulkan type.
    if(file MATCHES "/engine/include/render/.*\\.h$")
        set(header_allowed FALSE)
        foreach(ok ${RENDER_HEADER_BACKEND_ALLOWED})
            if(file MATCHES "${ok}$")
                set(header_allowed TRUE)
                break()
            endif()
        endforeach()
        if(NOT header_allowed)
            string(REGEX MATCHALL "#[ \t]*include[ \t]*<graphics/vulkan/[A-Za-z0-9_]+\\.h>"
                backend_hits "${content}")
            foreach(hit ${backend_hits})
                if(NOT hit MATCHES "Renderer\\.h")
                    string(REGEX REPLACE ".*<(graphics/vulkan/[A-Za-z0-9_]+\\.h)>.*" "\\1"
                        backend_header "${hit}")
                    list(APPEND VIOLATIONS
                        "${rel} is a render header including ${backend_header} -- only graphics/vulkan/Renderer.h may cross into a render header")
                endif()
            endforeach()
        endif()
    endif()

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
        # No semicolon in the message: VIOLATIONS is a CMake list, so a
        # semicolon inside an element splits it in two and both the report and
        # the count come out wrong.
        list(APPEND VIOLATIONS
            "${rel} names a Vulkan symbol (${type_hit}) -- render policy must not depend on the backend")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "render isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "render isolation check failed: ${n} violation(s)")
endif()

list(LENGTH RENDER_RECORDING_SET recording_count)
list(LENGTH RENDER_VULKAN_PENDING pending_count)
message(STATUS
    "render isolation OK (${recording_count} recording file(s) by contract, "
    "${pending_count} awaiting cleanup)")
