#pragma once

#include "EditorLinePipeline.h"
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
                      const EditorOverlayState& overlay, EditorLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const EditorScene& scene) override;
    void DrawWireframe(const FrameContext& frame, const EditorViewport& viewport,
                       const EditorScene& scene, const Vec4& color);

private:
    void AppendBrushMesh(std::vector<EditorLineVertex>& vertices,
                         const BrushMesh& mesh,
                         const Transform3f& transform,
                         const Vec4& color) const;

    SelectionService& Selection;
    const EditorOverlayState& Overlay;
    EditorLinePipeline& Lines;
};
