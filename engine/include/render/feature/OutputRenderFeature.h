#pragma once

#include <graphics/RenderFeature.h>
#include <graphics/vulkan/OutputTransformPass.h>

// Runs the output pass for one presentation. Every presentation with MainColor
// features needs one of these, or its scene never reaches the window.
class OutputRenderFeature final : public IRenderFeature
{
public:
    // `params` outlives the feature and is read each frame.
    explicit OutputRenderFeature(const OutputTransformParams& params) : Params(&params) {}

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Output; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

private:
    const OutputTransformParams* Params = nullptr;
    OutputTransformPass Pass;
};
