#include <render/MeshRenderFeature.h>

MeshRenderFeature::MeshRenderFeature(RenderQueue& queue,
                                     StaticMeshCache& meshes,
                                     MaterialCache& materials,
                                     const CameraRenderData& camera,
                                     const RenderLightSet& lights,
                                     std::shared_ptr<SpotShadowResources> shadows)
    : Queue(&queue)
    , Meshes(&meshes)
    , Materials(&materials)
    , Camera(&camera)
    , Lights(&lights)
    , Shadows(std::move(shadows))
{
}

void MeshRenderFeature::Setup(const RendererServices& services)
{
    Pass.Setup(services, *Shadows);
}

void MeshRenderFeature::OnDraw(const FrameContext& frame)
{
    if (Queue == nullptr || Lights == nullptr) return;
    Pass.Draw(frame, *Camera, *Lights, *Queue, *Meshes, *Materials);
}

void MeshRenderFeature::Teardown()
{
    Pass.Teardown();
}
