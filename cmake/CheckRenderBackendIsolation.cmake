# Enforces the render backend firewall: the graphics API (Vulkan today, D3D12
# later) is an implementation detail of the backend and must not leak into the
# render layer or anywhere above it.
#
#   - No engine file outside engine/**/graphics/vulkan/ may include a Vulkan or
#     VMA header (<vulkan/...>, <vk_mem_alloc...>), name a Vk*/Vma* type, or
#     call a vk* entry point. The render layer sees backend-neutral rhi types
#     (rhi::Extent2D and the neutral descriptors that follow) instead.
#   - The allowlist below holds the sites that still touch Vulkan directly while
#     the migration in docs/plans/rhi-backend-seam.md is in progress. Each entry
#     names the step that retires it, so the list shrinks to just the backend
#     directories once the migration completes. Adding a new site is a conscious
#     decision that belongs here, on the record.
#
# Matches Vk[A-Z]/vk[A-Z] (not VK_) on purpose: VK_* are config macros that can
# appear legitimately; a Vk* type or vk* call is a backend use, which is the
# leak this catches. This mirrors the JPH:: vs JPH_ distinction in
# CheckPhysicsIsolation.cmake.
#
# Run standalone (no build/Vulkan SDK needed):
#   cmake -P cmake/CheckRenderBackendIsolation.cmake

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Files/dirs allowed to touch the Vulkan backend directly. Anything matching one
# of these path fragments is skipped.
set(RENDER_BACKEND_ALLOWED
    "/engine/src/graphics/vulkan/"                  # the backend itself
    "/engine/include/graphics/vulkan/"              # the backend itself
    "/engine/src/render/MeshForwardPass.cpp"        # retired by Step 2 (rhi command surface)
    "/engine/include/render/MeshForwardPass.h"      # retired by Step 2
    "/engine/src/debug/ImGuiDebugOverlay.cpp"       # retired by the per-backend overlay split
    "/engine/include/debug/ImGuiDebugOverlay.h"     # retired by the per-backend overlay split
    "/engine/src/app/DefaultRenderPipeline.cpp"     # retired by Step 3 (neutral swapchain access)
)

set(VIOLATIONS "")

file(GLOB_RECURSE ENGINE_FILES
    "${REPO}/engine/include/*"
    "${REPO}/engine/src/*"
)
foreach(file ${ENGINE_FILES})
    set(allowed FALSE)
    foreach(ok ${RENDER_BACKEND_ALLOWED})
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

    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]vulkan/" vk_inc "${content}")
    if(vk_inc)
        list(APPEND VIOLATIONS "${rel} includes a <vulkan/...> header (Vulkan must stay behind graphics/vulkan/)")
    endif()

    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]vk_mem_alloc" vma_inc "${content}")
    if(vma_inc)
        list(APPEND VIOLATIONS "${rel} includes vk_mem_alloc.h (VMA is a backend detail)")
    endif()

    string(REGEX MATCH "Vk[A-Z]" vk_type "${content}")
    if(vk_type)
        list(APPEND VIOLATIONS "${rel} names a Vk* type (backend types stay behind graphics/vulkan/)")
    endif()

    string(REGEX MATCH "vk[A-Z]" vk_call "${content}")
    if(vk_call)
        list(APPEND VIOLATIONS "${rel} calls a vk* entry point (backend calls stay behind graphics/vulkan/)")
    endif()

    string(REGEX MATCH "Vma[A-Z]" vma_type "${content}")
    if(vma_type)
        list(APPEND VIOLATIONS "${rel} names a Vma* type (VMA is a backend detail)")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "render backend isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "render backend isolation check failed: ${n} violation(s)")
endif()

list(LENGTH ENGINE_FILES engine_count)
message(STATUS "render backend isolation OK (Vulkan confined to graphics/vulkan/ + the migration allowlist)")
