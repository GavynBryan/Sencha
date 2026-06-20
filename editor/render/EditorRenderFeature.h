#pragma once

#include "BrushSolidRenderer.h"
#include "ComponentVisualRenderer.h"
#include "EditorLinePipeline.h"
#include "EditorSolidPipeline.h"
#include "GpuGridRenderer.h"
#include "IBrushBodyRenderer.h"
#include "SelectionRenderer.h"
#include "ViewportBackdropRenderer.h"
#include "WireframeRenderer.h"

#include "../viewport/ViewportShading.h"

#include <graphics/vulkan/Renderer.h>

#include <array>

class LevelScene;
class ManipulatorSession;
class PreviewBuffer;
class SelectionService;
class ViewportLayout;
struct GridSettings;

class EditorRenderFeature : public IRenderFeature
{
public:
    EditorRenderFeature(ViewportLayout& viewportLayout,
                        LevelScene& scene,
                        SelectionService& selection,
                        PreviewBuffer& preview,
                        ManipulatorSession& session,
                        const GridSettings& grid);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    ViewportLayout& Layout;
    const GridSettings&    GridCfg;
    ViewportBackdropRenderer Backdrop;
    GpuGridRenderer        Grid;
    // Declared before the renderers that bind a reference to it at construction.
    // (The feature owns the one shared solid pipeline.)
    EditorSolidPipeline    Solid;
    BrushSolidRenderer     BrushSolid;
    // Declared before the line renderers: they bind a reference to it at
    // construction. (The feature owns the one shared line pipeline.)
    EditorLinePipeline     Lines;
    WireframeRenderer      Wireframe;
    ComponentVisualRenderer Visuals;
    SelectionRenderer      Highlight;
    // Brush-body strategy per ViewportShading; the draw loop indexes this by the
    // viewport's shading. A new shading mode registers its strategy here — the
    // draw loop never changes.
    std::array<IBrushBodyRenderer*, ViewportShadingCount> BodyRenderers{};
    Logger*                Log            = nullptr;
    bool                   LoggedFirstDraw = false;
};
