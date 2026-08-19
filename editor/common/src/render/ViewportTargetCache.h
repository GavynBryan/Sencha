#pragma once

#include "ImGuiTargetPresenter.h"
#include "viewport/ViewportId.h"

#include <graphics/GpuFrameRetirement.h>
#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>

#include <imgui.h>

#include <cstdint>
#include <span>
#include <vector>

// Maps editor viewports onto engine render targets: one scene target (HDR
// colour + depth) plus two full-resolution bloom ping-pong planes per viewport,
// shown in the UI through ImGui::Image.
//
// The images, their per-frame-in-flight slots, resizing, and retirement all
// belong to RenderTargetStore now; what is left here is the editor's own
// business -- which ViewportId owns which targets, the bloom pair being a
// property of an editor viewport rather than of targets in general, and the
// ImGui binding, which the engine must not know about.
//
// The render side (Offscreen phase) calls BeginFrame then AcquireForRender per
// viewport; the UI side (panel draw) calls Display to record the on-screen size
// and fetch the current texture. There is a one-frame size lag -- the render
// uses the size the panel recorded last frame -- which is the lag the
// screen-rect rendering already had.
class ViewportTargetCache
{
public:
    void Setup(const RendererServices& services);
    void Teardown();

    struct RenderView
    {
        VkImage       ColorImage = VK_NULL_HANDLE;
        VkImage       DepthImage = VK_NULL_HANDLE;
        VkImageView   ColorView = VK_NULL_HANDLE;
        VkImageView   DepthView = VK_NULL_HANDLE;
        VkExtent2D    Extent{};
        VkImageLayout* ColorLayout = nullptr; // caller updates after its transitions
        // Bloom scratch: two full-res ping-pong targets + the bindless sample indices.
        VkImage        BloomImage[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkImageView    BloomView[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkImageLayout* BloomLayout[2]{ nullptr, nullptr };
        VkExtent2D     BloomExtent{};
        uint32_t       BloomBindless[2]{ UINT32_MAX, UINT32_MAX };
    };

    // -- Render side (Offscreen phase), in this order ------------------------
    void BeginFrame(uint32_t frameInFlightIndex, GpuFrameRetirement retirement);
    // The current slot's images for `id`, sized to what the panel last
    // requested. nullopt until the panel has requested a size.
    [[nodiscard]] std::optional<RenderView> AcquireForRender(ViewportId id);
    // Destroy targets for viewports not in `live` (closed or re-laid-out).
    void Prune(std::span<const ViewportId> live);

    // -- UI side (panel draw) ------------------------------------------------
    // Record the on-screen pixel size and return the current slot's texture to
    // display (0 until the first render fills it).
    [[nodiscard]] ImTextureID Display(ViewportId id, VkExtent2D extent);

private:
    struct Entry
    {
        ViewportId Id{};
        RenderTargetId Scene;
        RenderTargetId Bloom[2];
    };

    [[nodiscard]] Entry* Find(ViewportId id);
    [[nodiscard]] Entry& FindOrAdd(ViewportId id);
    void Release(Entry& entry);

    RendererServices Services{};
    RenderTargetStore Store;
    ImGuiTargetPresenter Presenter;
    std::vector<Entry> Entries;
};
