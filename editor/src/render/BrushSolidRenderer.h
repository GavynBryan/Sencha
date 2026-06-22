#pragma once

#include "EditorSolidPipeline.h"
#include "IBrushBodyRenderer.h"

#include "../level/LevelScene.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>

#include <vector>

// Draws brushes as a solid, lit, UV-checkered preview so per-face texturing is
// visible and resize keeps texel density constant (04- S5). Triangulates each
// face, evaluates the per-face UV projection per vertex, and submits to the
// shared solid pipeline. Gathers only — owns no GPU state. The Solid shading
// strategy; the render feature runs it for viewports whose ViewportShading is
// Solid (it does not test orientation itself).
class BrushSolidRenderer : public IBrushBodyRenderer
{
public:
    BrushSolidRenderer(LevelScene& scene, EditorSolidPipeline& solid);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport) override;

private:
    void AppendBrushMesh(std::vector<EditorSolidVertex>& vertices,
                         const BrushMesh& mesh,
                         const Transform3f& transform) const;

    LevelScene& Scene;
    EditorSolidPipeline& Solid;
};
