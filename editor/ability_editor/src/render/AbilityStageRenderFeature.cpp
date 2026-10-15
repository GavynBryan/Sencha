#include "render/AbilityStageRenderFeature.h"

#include <algorithm>

namespace
{
// The least a framed stage spans, so a pawn standing alone is not filled edge to edge.
constexpr float kMinFrameRadius = 2.5f;
}

bool AbilityStageRenderFeature::Setup(const RenderFeatureServices& services)
{
    Picture.Setup(*services.Backend, "ability_stage");
    WideLines.Setup(*services.Backend);
    return true;
}

void AbilityStageRenderFeature::Teardown()
{
    WideLines.Teardown();
    Picture.Teardown();
}

bool AbilityStageRenderFeature::Frame(const Aabb3d& bounds)
{
    if (!bounds.IsValid())
        return false;
    Camera.Frame(bounds.Center(), std::max(kMinFrameRadius, bounds.HalfExtent().Magnitude()));
    return true;
}

void AbilityStageRenderFeature::OnDraw(const RenderFrame& renderFrame)
{
    const FrameContext& frame = *renderFrame.Backend;
    const std::optional<RenderTargetView> target = Picture.Begin(frame);
    if (!target)
        return;
    Picture.Draw(frame, *target, { { 0.025f, 0.03f, 0.045f, 1.0f } }, [&](const RenderScope& rendering) {
        const VkExtent2D extent = target->Extent;
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        WideLines.Submit(rendering.Context(),
                         Rect2d(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height)),
                         Camera.BuildRenderData(aspect), Strokes, /*onTop*/ true, "AbilityStage.strokes");
    });
}
