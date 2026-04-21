#pragma once

#include "GpuGridRenderer.h"

#include <graphics/vulkan/Renderer.h>

class FourWayViewportLayout;

class EditorRenderFeature : public IRenderFeature
{
public:
    explicit EditorRenderFeature(FourWayViewportLayout& viewportLayout);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    FourWayViewportLayout& ViewportLayout;
    GpuGridRenderer        Grid;
    Logger*                Log            = nullptr;
    bool                   LoggedFirstDraw = false;
};
