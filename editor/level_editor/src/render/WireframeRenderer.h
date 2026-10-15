#pragma once

#include <graphics/vulkan/ColoredVertexPipelines.h>
#include "IBrushBodyRenderer.h"

#include "document/EditorScene.h"
#include "overlay/EditorOverlayState.h"
#include "selection/SelectionService.h"
#include "viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

#include <vector>

// Draws brush mesh wireframes via the shared editor line pipeline. Owns no GPU
// state of its own. The Wireframe shading strategy; the render feature runs it
// for viewports whose ViewportShading is Wireframe.
class WireframeRenderer : public IBrushBodyRenderer
{
public:
    WireframeRenderer(SelectionService& selection,
                      const EditorOverlayState& overlay, ColoredVertexPipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const CameraRenderData& camera,
                      const EditorScene& scene) override;
    // The context-zone caller passes a pre-dimmed color.
    void DrawWireframe(const FrameContext& frame, const EditorViewport& viewport,
                       const CameraRenderData& camera,
                       const EditorScene& scene, const Vec4& color);

private:
    void AppendBrushMesh(std::vector<ColoredVertex>& vertices,
                         const BrushMesh& mesh,
                         const Transform3f& transform,
                         const Vec4& color) const;

    SelectionService& Selection;
    const EditorOverlayState& Overlay;
    ColoredVertexPipeline& Lines;
};
