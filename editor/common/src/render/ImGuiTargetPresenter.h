#pragma once

#include <graphics/GpuFrameRetirement.h>
#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/RenderTargetStore.h>

#include <imgui.h>

#include <cstdint>
#include <vector>

//=============================================================================
// ImGuiTargetPresenter
//
// The editor half of displaying a RenderTargetStore target: one ImGui
// descriptor set per target, rebuilt whenever the store hands back a different
// image view, and freed only once the frame that last sampled it has retired.
//
// This is deliberately not in the engine. The store owns images and lifetime;
// which UI toolkit reads them is the host's business, and the engine must not
// link ImGui to serve one.
//
// The rebuild trigger is the view handle itself rather than a notification
// from the store: a resize destroys and recreates the slot's image, so a
// changed view *is* the signal, and there is no second piece of state to keep
// in agreement with the first.
//=============================================================================
class ImGuiTargetPresenter
{
public:
    // `sampler` is used for every set. Nearest, not linear, for targets shown
    // 1:1: bilinear resampling beats against 1px grid and wireframe lines, and
    // nearest copies pixels verbatim so the composite matches rendering
    // straight to the swapchain.
    void Setup(VkSampler sampler);

    // Frees sets retired by frames the GPU has finished with.
    void BeginFrame(GpuFrameRetirement retirement);

    // The texture id for `id`'s current slot, creating or rebuilding the set
    // when the underlying view changed. 0 before the target has ever rendered.
    [[nodiscard]] ImTextureID Present(RenderTargetStore& store, RenderTargetId id);

    // Retires the set for `id`. Call before destroying the target.
    void Release(RenderTargetId id);

    // Frees everything immediately; the device must be idle.
    void Teardown();

private:
    struct Binding
    {
        RenderTargetId Id;
        // The view the set was built against. A different one means the store
        // rebuilt the image and the set now points at freed memory.
        VkImageView View = VK_NULL_HANDLE;
        VkDescriptorSet Set = VK_NULL_HANDLE;
    };

    struct Retired
    {
        VkDescriptorSet Set = VK_NULL_HANDLE;
        // Frame this set stopped being displayed. ImGui frees the set
        // immediately on request, so one still being sampled by an in-flight
        // frame has to outlive it.
        std::uint64_t Stamp = 0;
    };

    void Retire(VkDescriptorSet set);
    void Flush(bool force);

    VkSampler Sampler = VK_NULL_HANDLE;
    std::vector<Binding> Bindings;
    std::vector<Retired> RetiredSets;
    GpuFrameRetirement Retirement;
};
