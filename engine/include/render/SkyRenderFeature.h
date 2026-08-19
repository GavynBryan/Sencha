#pragma once

#include <graphics/vulkan/SkyGradientPass.h>
#include <render/Camera.h>
#include <render/RenderLight.h>

//=============================================================================
// SkyRenderFeature
//
// IRenderFeature that draws the background gradient. Runs in
// RenderPhase::MainColor and must be registered before MeshRenderFeature:
// registration order is draw order within a phase, and the pass fills the whole
// view without a depth test.
//
// The bridge, and the only place that decides where the gradient's colours come
// from. The pass takes a matrix and two colours; this reads them off the frame's
// light set, which is where the cvars land today and where an authored
// environment record would land tomorrow.
//=============================================================================
class SkyRenderFeature : public IRenderFeature
{
public:
    SkyRenderFeature(const CameraRenderData& camera, const RenderLightSet& lights);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    [[nodiscard]] bool Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    const CameraRenderData* Camera = nullptr;
    const RenderLightSet* Lights = nullptr;
    SkyGradientPass Pass;
};
