#pragma once

#include "render/DisplayLedger.h"

#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>

#include <cstdint>
#include <optional>
#include <vector>

// Offscreen targets a host image shows, resident only while shown: a target the
// UI did not show last frame has its images evicted and does not render, and
// the frame it is shown again rebuilds it once.
class DisplayedTargets
{
public:
    void Setup(const RendererServices& services);
    // Waits the device out; call from a feature's Teardown.
    void Teardown();

    [[nodiscard]] RenderTargetId Create(const RenderTargetDesc& desc);
    void Destroy(RenderTargetId id);

    // Render side, once per frame before any Acquire.
    void BeginFrame(std::uint32_t frameInFlightIndex);
    // This frame's images, only for a target displayed last frame.
    [[nodiscard]] std::optional<RenderTargetView> Acquire(RenderTargetId id);

    // UI side: records the on-screen size, so it renders next frame at that
    // size; a host image then shows it (`PublishTargetImage`).
    void Show(RenderTargetId id, VkExtent2D extent);

private:
    RenderTargetStore Store;
    DisplayLedger<RenderTargetId> Ledger;
    std::vector<RenderTargetId> Created;
};
