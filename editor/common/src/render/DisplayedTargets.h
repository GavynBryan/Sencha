#pragma once

#include "render/DisplayLedger.h"
#include "render/ImGuiTargetPresenter.h"

#include <graphics/GpuFrameRetirement.h>
#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <vector>

// Offscreen targets a panel shows through ImGui, resident only while shown: a
// target the UI did not display last frame has its images evicted and does not
// render, and the frame it is displayed again rebuilds it once.
class DisplayedTargets
{
public:
    void Setup(const RendererServices& services, VkSampler sampler);
    // Waits the device out; call from a feature's Teardown.
    void Teardown();

    [[nodiscard]] RenderTargetId Create(const RenderTargetDesc& desc);
    void Destroy(RenderTargetId id);

    // Render side, once per frame before any Acquire.
    void BeginFrame(std::uint32_t frameInFlightIndex, GpuFrameRetirement retirement);
    // This frame's images, only for a target displayed last frame.
    [[nodiscard]] std::optional<RenderTargetView> Acquire(RenderTargetId id);

    // UI side: records the on-screen size and returns the texture to draw, 0
    // until a render has filled it.
    [[nodiscard]] ImTextureID Display(RenderTargetId id, VkExtent2D extent);

private:
    RenderTargetStore Store;
    ImGuiTargetPresenter Presenter;
    DisplayLedger<RenderTargetId> Ledger;
    std::vector<RenderTargetId> Created;
};
