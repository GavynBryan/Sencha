#pragma once

#include "ComponentVisualRenderer.h"
#include "GpuGridRenderer.h"
#include "SelectionRenderer.h"
#include "WireframeRenderer.h"

#include <graphics/vulkan/Renderer.h>

class LevelScene;
class ManipulatorSession;
class PreviewBuffer;
class SelectionService;
class ViewportLayout;

class EditorRenderFeature : public IRenderFeature
{
public:
    EditorRenderFeature(ViewportLayout& viewportLayout,
                        LevelScene& scene,
                        SelectionService& selection,
                        PreviewBuffer& preview,
                        ManipulatorSession& session);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    ViewportLayout& Layout;
    GpuGridRenderer        Grid;
    WireframeRenderer      Wireframe;
    ComponentVisualRenderer Visuals;
    SelectionRenderer      Highlight;
    Logger*                Log            = nullptr;
    bool                   LoggedFirstDraw = false;
};
