#include <render/MeshRenderFeature.h>

MeshRenderFeature::MeshRenderFeature(RenderQueue& queue,
                                     StaticMeshCache& meshes,
                                     MaterialCache& materials,
                                     const CameraRenderData& camera)
    : Queue(&queue)
    , Meshes(&meshes)
    , Materials(&materials)
    , Camera(&camera)
{
}

void MeshRenderFeature::Setup(const RendererServices& services)
{
    Pass.Setup(services);
}

void MeshRenderFeature::OnDraw(const FrameContext& frame)
{
    if (Queue == nullptr) return;
    Pass.Draw(frame, *Camera, *Queue, *Meshes, *Materials);
}

void MeshRenderFeature::Teardown()
{
    Pass.Teardown();
}
