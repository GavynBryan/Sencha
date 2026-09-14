#pragma once

#include "EditorInstancedLinePipeline.h"
#include "IBrushBodyRenderer.h"

#include <math/Vec.h>

#include <vector>

class BrushDrawSet;
class SceneRenderQueueBuilder;
class SelectionService;
struct EditorOverlayState;

//=============================================================================
// SceneWireframeRenderer
//
// The Wireframe brush body strategy over a queue builder's retained draw set:
// each brush entity's distinct meshes are drawn as instanced line lists over
// the entity's retained placements, so a frame copies edge topology and
// matrices and transforms nothing. The wireframe twin of SceneSolidRenderer;
// the immediate WireframeRenderer remains only where no builder exists.
//=============================================================================
class SceneWireframeRenderer : public IBrushBodyRenderer
{
public:
    SceneWireframeRenderer(SelectionService& selection, const EditorOverlayState& overlay,
                           const SceneRenderQueueBuilder& queues,
                           EditorInstancedLinePipeline& lines);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport,
                      const CameraRenderData& camera,
                      const EditorScene& scene) override;

    // Any document's retained draws in one colour; the context-zone caller
    // passes a pre-dimmed colour and its own builder's draw set.
    void DrawWireframe(const FrameContext& frame, const EditorViewport& viewport,
                       const CameraRenderData& camera, const EditorScene& scene,
                       const BrushDrawSet& draws, const Vec4& color);

private:
    SelectionService& Selection;
    const EditorOverlayState& Overlay;
    const SceneRenderQueueBuilder& Queues;
    EditorInstancedLinePipeline& Lines;
    std::vector<EditorLineVertex> EdgeScratch; // one mesh's coloured edges, reused
};
