#include "MaterialPreviewRenderFeature.h"

#include <assets/runtime/RuntimeAssets.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/RenderScope.h>
#include <math/geometry/3d/Frustum.h>
#include <render/CameraProjection.h>

#include <algorithm>
#include <cmath>

namespace
{
    // The cache keys targets by ViewportId; the preview has exactly one view.
    constexpr ViewportId kPreviewView{ 1 };

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
    Forward.Teardown();
    Lighting.Teardown();
    Backdrop.Teardown();
    Targets.Teardown();
}

void MaterialPreviewRenderFeature::ReleaseResources()
{
    for (StaticMeshHandle& mesh : Meshes)
    {
        if (mesh.IsValid())
            Assets.Assets.ReleaseStaticMesh(mesh);
        mesh = StaticMeshHandle{};
    }
    Material = MaterialHandle{};
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
    return Targets.Display(kPreviewView, extent);
}

void MaterialPreviewRenderFeature::OnDraw(const RenderFrame& renderFrame)
{
    const FrameContext& frame = *renderFrame.Backend;
    Targets.BeginFrame(frame.FrameInFlightIndex, frame.Retirement);
    const std::optional<ViewportTargetCache::RenderView> target = Targets.AcquireForRender(kPreviewView);
    if (!target)
        return;

    const auto transitionColor = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
                                     VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                                     VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess)
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = target->ColorImage;
        t.OldLayout = oldLayout;
        t.NewLayout = newLayout;
        t.SrcStage = srcStage;
        t.DstStage = dstStage;
        t.SrcAccess = srcAccess;
        t.DstAccess = dstAccess;
        t.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        VulkanBarriers::TransitionImage(frame.Cmd, t);
    };

    transitionColor(*target->ColorLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    {
        VulkanBarriers::ImageTransition t{};
        t.Image = target->DepthImage;
        t.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        t.NewLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        t.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.DstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        t.SrcAccess = 0;
        t.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                    | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        t.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        VulkanBarriers::TransitionImage(frame.Cmd, t);
    }

    RenderScopeDesc scope{};
    scope.Area.offset = { 0, 0 };
    scope.Area.extent = target->Extent;
    scope.Color.View = target->ColorView;
    scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Black base; the backdrop draw paints the glowing grid over it.
    scope.Color.Clear.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
    scope.ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    scope.Depth.View = target->DepthView;
    scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    scope.Depth.Clear.depthStencil = { 1.0f, 0 };
    scope.DepthFormat = Services.DepthFormat;
    scope.Phase = RenderPhase::Offscreen;

    {
        const RenderScope rendering(frame, scope);

        Backdrop.Draw(frame.Cmd, target->Extent, VK_FORMAT_R16G16B16A16_SFLOAT,
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
                item.Pass = resolved->Pass;
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

    transitionColor(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    *target->ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
