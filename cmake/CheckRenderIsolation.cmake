# Enforces the render-domain firewall: engine/{include,src}/render is render
# policy, and policy must be readable, testable, and debuggable without a
# graphics API in scope.
#
# The one licensed exception is the recording set, render/pass/: the three
# bridge types that turn render-domain state into device commands. A recording
# pass takes RenderQueue, MaterialCache, and StaticMeshCache by reference, so
# moving one into the backend would invert the dependency it is there to keep
# pointing downward. Those files name Vulkan permanently and correctly -- they
# are the per-backend surface a second backend would twin. The set is CLOSED:
# render/pass/ may contain exactly the listed files, and a new entry is a
# claim that the renderer grew a fourth recording surface, which is a design
# review, not a checkbox.
#
# Everything else under render/ -- headers and implementation files alike --
# must not name the backend at all:
#
#   - no <vulkan/...> or <vk_mem_alloc.h> includes,
#   - no <graphics/vulkan/...> includes (the neutral contract is
#     graphics/RenderFeature.h; device resources go through graphics/GpuBuffers
#     and graphics/GpuImages),
#   - no Vk* / vk* / VK_* symbols,
#   - no identifiers beginning with "Vulkan" (a forward-declared
#     VulkanBufferService is still a backend dependency),
#   - no <render/pass/...> includes outside the driver list below.
#
# RENDER_PASS_DRIVERS are the files licensed to drive the recording set: the
# render/feature/ units, whose whole job is handing render-domain state to a
# pass, plus ProbeVolumeSet.cpp, which publishes probe images into the
# lighting bindings. Two feature headers additionally hold a backend pass by
# value (the SkyRenderFeature precedent: the pass takes plain data and names
# no render-domain type, so the recording set did not grow); each is licensed
# for exactly the one pass header it owns, nothing else.
#
# Scope note: matching is textual, so this catches *direct* includes and
# names only. A render header that reaches Vulkan through an innocently-named
# engine header will not trip it; measure the transitive closure with the
# compiler when auditing:
#
#   echo '#include <render/Foo.h>' | g++ -std=c++20 -x c++ -Iengine/include -M - \
#       | tr ' \\' '\n\n' | grep -E 'vulkan/vulkan\.h|vk_mem_alloc\.h'
#
# Comments are stripped before matching. Prose that names VkFormat or
# VK_FORMAT_R8G8B8A8_UNORM to say what a CPU-side enum maps to is
# documentation doing its job, not a dependency. MakeVulkanPerspective and
# MakeVulkanOrthographic pass the identifier rule on purpose: "Vulkan" there
# names the recorded clip-space convention (Y flip, [0,1] depth), not a
# backend type, and the rule anchors on identifiers that *begin* with Vulkan.
#
# Run standalone (no build needed):
#   cmake -P cmake/CheckRenderIsolation.cmake

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# The recording set. Closed by design -- see the header comment.
set(RENDER_RECORDING_SET
    "/engine/include/render/pass/LightBindings.h"   # descriptor set 2, shadow pool images
    "/engine/include/render/pass/MeshForwardPass.h" # forward draw recording
    "/engine/include/render/pass/ShadowDepthPass.h" # shadow view recording
    "/engine/src/render/pass/LightBindings.cpp"
    "/engine/src/render/pass/MeshForwardPass.cpp"
    "/engine/src/render/pass/ShadowDepthPass.cpp"
)

# Files licensed to include <render/pass/...>.
set(RENDER_PASS_DRIVERS
    "/engine/include/render/feature/MeshRenderFeature.h"
    "/engine/include/render/feature/ShadowRenderFeature.h"
    "/engine/include/render/feature/SkyRenderFeature.h"
    "/engine/include/render/feature/SkinnedPoseRenderFeature.h"
    "/engine/src/render/feature/MeshRenderFeature.cpp"
    "/engine/src/render/feature/ShadowRenderFeature.cpp"
    "/engine/src/render/feature/SkyRenderFeature.cpp"
    "/engine/src/render/feature/SkinnedPoseRenderFeature.cpp"
    "/engine/src/render/ProbeVolumeSet.cpp"  # publishes probe images into LightBindings
)

# file-suffix=backend-header pairs: the one graphics/vulkan header each of
# these may include, because it holds that pass by value.
set(RENDER_DRIVER_BACKEND_PAIRS
    "/engine/include/render/feature/SkyRenderFeature.h=graphics/vulkan/SkyGradientPass.h"
    "/engine/include/render/feature/SkinnedPoseRenderFeature.h=graphics/vulkan/SkinnedPosePass.h"
)

set(VIOLATIONS "")

# The closed-set rule: render/pass/ holds exactly the recording set.
file(GLOB_RECURSE PASS_FILES
    "${REPO}/engine/include/render/pass/*"
    "${REPO}/engine/src/render/pass/*"
)
foreach(file ${PASS_FILES})
    file(RELATIVE_PATH rel "${REPO}" "${file}")
    set(listed FALSE)
    foreach(ok ${RENDER_RECORDING_SET})
        if(file MATCHES "${ok}$")
            set(listed TRUE)
            break()
        endif()
    endforeach()
    if(NOT listed)
        list(APPEND VIOLATIONS
            "${rel} is in render/pass/ but not in the recording set -- a new recording surface is a design review, not a checkbox")
    endif()
endforeach()

file(GLOB_RECURSE RENDER_FILES
    "${REPO}/engine/include/render/*"
    "${REPO}/engine/src/render/*"
)
foreach(file ${RENDER_FILES})
    # The recording set is exempt from every content rule.
    set(is_pass FALSE)
    foreach(ok ${RENDER_RECORDING_SET})
        if(file MATCHES "${ok}$")
            set(is_pass TRUE)
            break()
        endif()
    endforeach()
    if(is_pass)
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

    # Backend headers. A driver pair licenses exactly one; nothing else in
    # render/ may include graphics/vulkan/ at all.
    string(REGEX MATCHALL "#[ \t]*include[ \t]*<graphics/vulkan/[A-Za-z0-9_]+\\.h>"
        backend_hits "${content}")
    foreach(hit ${backend_hits})
        string(REGEX REPLACE ".*<(graphics/vulkan/[A-Za-z0-9_]+\\.h)>.*" "\\1"
            backend_header "${hit}")
        set(pair_ok FALSE)
        foreach(pair ${RENDER_DRIVER_BACKEND_PAIRS})
            string(REGEX REPLACE "=.*" "" pair_file "${pair}")
            string(REGEX REPLACE ".*=" "" pair_header "${pair}")
            if(file MATCHES "${pair_file}$" AND backend_header STREQUAL pair_header)
                set(pair_ok TRUE)
                break()
            endif()
        endforeach()
        if(NOT pair_ok)
            list(APPEND VIOLATIONS
                "${rel} includes ${backend_header} -- render code reaches the device through graphics/RenderFeature.h and the GpuBuffers/GpuImages surfaces, never the backend directly")
        endif()
    endforeach()

    # Recording-set headers propagate the backend; only the drivers take them.
    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]render/pass/" pass_inc "${content}")
    if(pass_inc)
        set(driver FALSE)
        foreach(ok ${RENDER_PASS_DRIVERS})
            if(file MATCHES "${ok}$")
                set(driver TRUE)
                break()
            endif()
        endforeach()
        if(NOT driver)
            list(APPEND VIOLATIONS
                "${rel} includes a render/pass/ header but is not a pass driver -- features drive passes; policy does not")
        endif()
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

    # Identifiers beginning with "Vulkan" (VulkanBufferService and kin), which
    # the Vk/vk/VK_ patterns miss. Anchored on a preceding non-identifier
    # character so convention-naming like MakeVulkanPerspective passes.
    string(REGEX MATCH "(^|[^A-Za-z0-9_])(Vulkan[A-Z][A-Za-z0-9_]*)"
        backend_name_hit "${content}")
    if(backend_name_hit)
        list(APPEND VIOLATIONS
            "${rel} names a backend type (${CMAKE_MATCH_2}) -- render policy must not depend on the backend")
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
message(STATUS
    "render isolation OK (${recording_count} recording file(s) by contract, closed set)")
