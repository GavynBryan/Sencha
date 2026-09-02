#include "MaterialPreviewRenderFeature.h"

#include <assets/runtime/RuntimeAssets.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <graphics/vulkan/RenderScope.h>
#include <math/geometry/3d/Frustum.h>
#include <render/CameraProjection.h>

#include <algorithm>
#include <cmath>

namespace
{
// HDR so the backdrop's glow and lit surfaces survive the tonemap, matching
// what a viewport renders into.
constexpr VkFormat kPreviewColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
}

MaterialPreviewRenderFeature::MaterialPreviewRenderFeature(RuntimeAssets& assets)
    : Assets(assets)
{
}

bool MaterialPreviewRenderFeature::Setup(const RenderFeatureServices& featureServices)
{
    const RendererServices& services = *featureServices.Backend;
    Services = services;
    Targets.Setup(services);
    if (services.Samplers != nullptr)
        Presenter.Setup(services.Samplers->GetLinearClamp());
    RenderTargetDesc scene{};
    scene.ColorFormat = kPreviewColorFormat;
    scene.DepthFormat = services.DepthFormat;
    // ImGui binds this itself, through the presenter.
    scene.Read = RenderTargetRead::Sampled;
    scene.DebugName = "material_preview";
    SceneTarget = Targets.Create(scene);
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
    Presenter.Release(SceneTarget);
    Targets.Teardown();
    Presenter.Teardown();
    SceneTarget = {};
}

void MaterialPreviewRenderFeature::Orbit(float yawDelta, float pitchDelta)
{
    Yaw += yawDelta;
    Pitch = std::clamp(Pitch + pitchDelta, -1.5f, 1.5f);
}

void MaterialPreviewRenderFeature::Zoom(float wheelDelta)
{
    Distance = std::clamp(Distance * (1.0f - wheelDelta * 0.1f), 0.6f, 8.0f);
}

ImTextureID MaterialPreviewRenderFeature::Display(VkExtent2D extent)
{
    Targets.SetExtent(SceneTarget, extent);
    return Presenter.Present(Targets, SceneTarget);
}

void MaterialPreviewRenderFeature::OnDraw(const RenderFrame& renderFrame)
{
    const FrameContext& frame = *renderFrame.Backend;
    Targets.BeginFrame(frame.FrameInFlightIndex);
    Presenter.BeginFrame(frame.Retirement);
    const std::optional<RenderTargetView> target = Targets.Acquire(SceneTarget);
    if (!target)
        return;

    // Brackets the recording below and commits the layout the store remembers.
    RenderTargetSession session(frame.Cmd, target->ColorImage, target->ColorLayout,
                                target->DepthImage);

    RenderScopeDesc scope{};
    scope.Area.offset = { 0, 0 };
    scope.Area.extent = target->Extent;
    scope.Color.View = target->ColorView;
    scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Black base; the backdrop draw paints the glowing grid over it.
    scope.Color.Clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    scope.ColorFormat = kPreviewColorFormat;
    scope.Depth.View = target->DepthView;
    scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    scope.Depth.Clear.depthStencil = { 1.0f, 0 };
    scope.DepthFormat = Services.DepthFormat;
    scope.Phase = RenderPhase::Offscreen;

    {
        const RenderScope rendering(frame, scope);

        Backdrop.Draw(frame.Cmd, target->Extent, kPreviewColorFormat,
                      Services.DepthFormat, BackdropStyle);

        if (Material.IsValid())
        {
            const float aspect = target->Extent.height > 0
                ? static_cast<float>(target->Extent.width) / static_cast<float>(target->Extent.height)
                : 1.0f;

            const Vec3d eye(Distance * std::cos(Pitch) * std::sin(Yaw),
                            Distance * std::sin(Pitch),
                            Distance * std::cos(Pitch) * std::cos(Yaw));

            CameraRenderData camera;
            camera.Position = eye;
            camera.View = Mat4::MakeLookAt(eye, Vec3d(0.0f, 0.0f, 0.0f), Vec3d(0.0f, 1.0f, 0.0f));
            camera.Projection = MakeVulkanPerspective(0.9f, aspect, 0.05f, 50.0f);
            camera.ViewProjection = camera.Projection * camera.View;
            camera.ViewFrustum = Frustum::FromViewProjection(camera.ViewProjection);

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
    }

    // Session ends here: sampled, and the store's layout committed.
}
