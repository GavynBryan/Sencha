#pragma once

#include <graphics/vulkan/DebugLinePass.h>
#include <render/extract/Camera.h>

// Draws the frame's extracted debug lines after the scene in MainColor, so the
// depth-tested ones are hidden by what stands in front of them.
class DebugLineRenderFeature : public IRenderFeature
{
public:
    DebugLineRenderFeature(const CameraRenderData& camera, const DebugLineFrame& lines);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

private:
    const CameraRenderData* Camera = nullptr;
    const DebugLineFrame* Lines = nullptr;
    DebugLinePass Pass;
};
