#include "SceneSolidRenderer.h"

#include "SceneRenderQueueBuilder.h"

#include "../viewport/EditorViewport.h"

SceneSolidRenderer::SceneSolidRenderer(MeshForwardPass& pass,
                                       const SceneRenderQueueBuilder& queues,
                                       StaticMeshCache& meshes,
                                       MaterialCache& materials)
    : Pass(pass)
    , Queues(queues)
    , Meshes(meshes)
    , Materials(materials)
{
}

void SceneSolidRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    Pass.Draw(frame, viewport.BuildRenderData(), Queues.Lights(), Queues.BrushQueue(), Meshes, Materials);
}
