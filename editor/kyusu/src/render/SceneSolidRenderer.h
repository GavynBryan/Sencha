#pragma once

#include "IBrushBodyRenderer.h"

#include <render/MeshForwardPass.h>

class SceneRenderQueueBuilder;
class StaticMeshCache;
class MaterialCache;

//=============================================================================
// SceneSolidRenderer
//
// The Solid brush body strategy: draws the builder's brush queue through the
// runtime MeshForwardPass, so a Solid viewport shows the real material + texture
// the game ships (WYSIWYG). Replaces the procedural-checker BrushSolidRenderer in
// BodyRenderers[Solid]; the IBrushBodyRenderer seam stays at two implementations
// (this + WireframeRenderer), so the per-viewport choice stays data-driven.
//=============================================================================
class SceneSolidRenderer : public IBrushBodyRenderer
{
public:
    SceneSolidRenderer(MeshForwardPass& pass,
                       const SceneRenderQueueBuilder& queues,
                       StaticMeshCache& meshes,
                       MaterialCache& materials);

    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport) override;

private:
    MeshForwardPass& Pass;
    const SceneRenderQueueBuilder& Queues;
    StaticMeshCache& Meshes;
    MaterialCache& Materials;
};
