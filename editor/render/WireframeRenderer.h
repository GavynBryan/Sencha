#pragma once

#include "EditorLinePipeline.h"
#include "IBrushBodyRenderer.h"

#include "../level/BrushGeometry.h"
#include "../level/LevelScene.h"
#include "../render/PreviewBuffer.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

#include <vector>

// Draws brush mesh wireframes (and the create-drag preview box) via the shared
// editor line pipeline. Owns no GPU state of its own — it only gathers lines.
// The Wireframe shading strategy; the render feature runs it for viewports whose
// ViewportShading is Wireframe.
class WireframeRenderer : public IBrushBodyRenderer
{
public:
    WireframeRenderer(LevelScene& scene, EditorLinePipeline& lines);

    void SetPreviewBuffer(PreviewBuffer* preview);
    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport) override;

private:
    void AppendBrush(std::vector<EditorLineVertex>& vertices,
                     const BrushState& brush,
                     const Vec4& color) const;
    void AppendBrushMesh(std::vector<EditorLineVertex>& vertices,
                         const BrushMesh& mesh,
                         const Transform3f& transform,
                         const Vec4& color) const;

    LevelScene& Scene;
    EditorLinePipeline& Lines;
    PreviewBuffer* Preview = nullptr;
};
