#include <render/MeshRenderFeature.h>

MeshRenderFeature::MeshRenderFeature(RenderQueue& queue,
                                     StaticMeshCache& meshes,
                                     MaterialCache& materials,
                                     const CameraRenderData& camera,
                                     const RenderLightSet& lights,
                                     std::shared_ptr<LightBindings> bindings)
    : Queue(&queue)
    , Meshes(&meshes)
    , Materials(&materials)
    , Camera(&camera)
    , Lights(&lights)
    , Bindings(std::move(bindings))
{
}

void MeshRenderFeature::Setup(const RendererServices& services)
{
    Pass.Setup(services, *Bindings);
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
