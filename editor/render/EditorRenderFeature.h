#pragma once

#include "GpuGridRenderer.h"
#include "SelectionRenderer.h"
#include "WireframeRenderer.h"

#include <graphics/vulkan/Renderer.h>

class FourWayViewportLayout;
class LevelScene;
class SelectionService;

class EditorRenderFeature : public IRenderFeature
{
public:
    EditorRenderFeature(FourWayViewportLayout& viewportLayout,
                        LevelScene& scene,
                        SelectionService& selection);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    FourWayViewportLayout& ViewportLayout;
    GpuGridRenderer        Grid;
    WireframeRenderer      Wireframe;
    SelectionRenderer      Highlight;
    Logger*                Log            = nullptr;
    bool                   LoggedFirstDraw = false;
};
