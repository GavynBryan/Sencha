#include <render/feature/OutlineRenderFeature.h>

OutlineRenderFeature::OutlineRenderFeature(MeshForwardPass& forward, OutlineFrame& outlines,
                                           const CameraRenderData& camera, const RenderLightSet& lights,
                                           StaticMeshCache& meshes, MaterialCache& materials,
                                           const SkinnedMeshCache* skinnedMeshes)
    : Forward(&forward)
    , Outlines(&outlines)
    , Camera(&camera)
    , Lights(&lights)
    , Meshes(&meshes)
    , Materials(&materials)
    , SkinnedMeshes(skinnedMeshes)
{
}

bool OutlineRenderFeature::Setup(const RenderFeatureServices& services)
{
    Targets.Setup(*services.Backend);
    return true;
}

void OutlineRenderFeature::Teardown()
{
    Targets.Teardown();
}

void OutlineRenderFeature::OnDraw(const RenderFrame& frame)
{
    if (Outlines->DrawOrder.empty())
    {
        Targets.NoteQuietFrame();
        return;
    }
    const auto attachments = Targets.Begin(*frame.Backend, Outlines->CompositeInputs());
    if (!attachments.has_value())
        return;
    Forward->DrawOutlineMask(*frame.Backend, *attachments, *Camera, *Lights, *Meshes, *Materials, SkinnedMeshes,
                             *Outlines);
    Targets.Publish(*frame.Backend, Outlines->CompositeInputs(), Camera->Projection);
}
