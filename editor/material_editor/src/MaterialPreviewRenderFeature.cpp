#include "MaterialPreviewRenderFeature.h"

#include <assets/runtime/RuntimeAssets.h>
#include <math/geometry/3d/Frustum.h>
#include <render/CameraProjection.h>

#include <algorithm>
#include <cmath>

MaterialPreviewRenderFeature::MaterialPreviewRenderFeature(RuntimeAssets& assets)
    : Assets(assets)
{
}

bool MaterialPreviewRenderFeature::Setup(const RenderFeatureServices& featureServices)
{
    const RendererServices& services = *featureServices.Backend;
    Services = services;
    Picture.Setup(services, "material_preview");
    Backdrop.Setup(services);
    if (!Lighting.Setup(services) && services.Logging != nullptr)
    {
        services.Logging->GetLogger<MaterialPreviewRenderFeature>().Warn(
            "Lighting bindings failed to set up; preview forward pass disabled");
    }
    Forward.Setup(services, Lighting);

    for (std::size_t i = 0; i < Meshes.size(); ++i)
    {
        const auto kind = static_cast<PreviewPrimitive>(i);
        Meshes[i] = Assets.StaticMeshes->CreateFromData(PreviewPrimitiveName(kind),
                                                        BuildPreviewPrimitive(kind));
    }
    // A failed lighting set disables the preview's forward pass, not the
    // feature: the backdrop still renders.
    return true;
}

void MaterialPreviewRenderFeature::Teardown()
{
    // Asset refs first: these borrow the editor's asset system, which outlives
    // this feature only because the host removes it before tearing that down.
    for (StaticMeshHandle& mesh : Meshes)
    {
        if (mesh.IsValid())
            Assets.Assets.ReleaseLease(AssetType::StaticMesh, mesh.ToToken());
        mesh = StaticMeshHandle{};
    }
    Material = MaterialHandle{};

    Forward.Teardown();
    Lighting.Teardown();
    Backdrop.Teardown();
    Picture.Teardown();
}

void MaterialPreviewRenderFeature::Orbit(float yawDelta, float pitchDelta)
{
    Camera.Orbit(yawDelta, pitchDelta);
}

void MaterialPreviewRenderFeature::Zoom(float wheelDelta)
{
    Camera.Zoom(wheelDelta);
}

void MaterialPreviewRenderFeature::Show(VkExtent2D extent, UiHostImageId image)
{
    Picture.Show(extent, image);
}

void MaterialPreviewRenderFeature::OnDraw(const RenderFrame& renderFrame)
{
    const FrameContext& frame = *renderFrame.Backend;
    const std::optional<RenderTargetView> target = Picture.Begin(frame);
    if (!target)
        return;
    // Black base; the backdrop draw paints the glowing grid over it.
    Picture.Draw(frame, *target, { { 0.0f, 0.0f, 0.0f, 1.0f } }, [&](const RenderScope& rendering) {
        Backdrop.Draw(frame.Cmd, target->Extent, StagePicture::kColorFormat, Services.DepthFormat, BackdropStyle);
        if (Material.IsValid())
            DrawMaterial(rendering, target->Extent);
    });
}

void MaterialPreviewRenderFeature::DrawMaterial(const RenderScope& rendering, VkExtent2D extent)
{
    const float aspect = extent.height > 0 ? static_cast<float>(extent.width) / static_cast<float>(extent.height) : 1.0f;

    const CameraRenderData camera = Camera.BuildRenderData(aspect);
    const Vec3d eye = camera.Position;

    // Key light rides above the camera's shoulder so orbiting keeps the lit
    // side facing the viewer.
    Lights.Reset();
    PointLightComponent key;
    key.Color = Vec<3>(1.0f, 1.0f, 1.0f);
    key.Intensity = LightIntensity;
    key.Range = 30.0f;
    Lights.AddPoint(eye * 1.5f + Vec3d(0.0f, 1.0f, 0.0f), key);

    // Through the same classifier a scene view uses, so the preview
    // shows the pipeline the material will actually draw with --
    // unlit, double-sided, masked, or blended. A hand-built opaque
    // item previewed every one of those through StandardLitBack.
    RenderQueueItem item;
    item.Mesh = Meshes[static_cast<std::size_t>(Active)];
    item.Material = Material;
    item.SectionIndex = 0;
    item.WorldMatrix = Mat4::Identity();
    Queue.Reset();
    if (const ::Material* resolved = Assets.Materials.Get(Material))
    {
        item.Pipeline = SelectOpaquePipeline(*resolved);
        item.Pass = ResolveMaterialPass(*resolved);
    }
    if (item.Pass == ShaderPassId::ForwardTransparent)
        Queue.AddTransparent(item);
    else
        Queue.AddOpaque(item);
    Queue.SortOpaque();

    Forward.Draw(rendering.Context(),
                 MeshForwardPass::DrawContext{ .Camera = camera,
                                               .Lights = Lights,
                                               .Queue = Queue,
                                               .Meshes = *Assets.StaticMeshes,
                                               .Materials = Assets.Materials });
}
