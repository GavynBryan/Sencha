#include "SceneSolidRenderer.h"

#include "SceneRenderQueueBuilder.h"

#include "viewport/EditorViewport.h"

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

void SceneSolidRenderer::DrawViewport(const FrameContext& frame, const EditorViewport&,
                                      const CameraRenderData& camera,
                                      const EditorScene&)
{
    // Brush cells are welded static geometry; the brush queue cannot carry a
    // skinned item, so there is no skinned cache to hand here.
    Pass.Draw(frame, camera, Queues.Lights(), Queues.BrushQueue(), Meshes, Materials);
}
