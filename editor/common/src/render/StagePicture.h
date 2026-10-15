#pragma once

#include "render/DisplayedTargets.h"

#include <graphics/vulkan/RenderScope.h>
#include <render/ui/UiDrawFrame.h>

#include <functional>
#include <optional>

struct FrameContext;

// One offscreen colour and depth target a host image shows: a preview stage's
// picture. It renders only while the UI shows it, and the host image is
// withdrawn whenever the target was not left readable this frame.
class StagePicture
{
public:
    // HDR, so glow and lit surfaces survive the tonemap as they do in a viewport.
    static constexpr VkFormat kColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    void Setup(const RendererServices& services, const char* debugName);
    void Teardown();

    // UI side: the size it is shown at and the host image showing it.
    void Show(VkExtent2D extent, UiHostImageId image);

    // Render side, once per frame: this frame's target, or none while it is not shown.
    [[nodiscard]] std::optional<RenderTargetView> Begin(const FrameContext& frame);
    // Records `draw` into `target` cleared to `clear`, then shows it.
    void Draw(const FrameContext& frame, const RenderTargetView& target, VkClearColorValue clear,
              const std::function<void(const RenderScope&)>& draw);

private:
    RendererServices Services{};
    DisplayedTargets Targets;
    RenderTargetId Target;
    std::optional<UiHostImageId> Image;
};
