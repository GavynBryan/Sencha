#include "MaterialPreviewRenderFeature.h"

#include <core/assets/RuntimeAssets.h>
#include <graphics/vulkan/VulkanBarriers.h>
#include <math/geometry/3d/Frustum.h>

#include <algorithm>
#include <cmath>

namespace
{
    // The cache keys targets by ViewportId; the preview has exactly one view.
    constexpr ViewportId kPreviewView{ 1 };

    // Same convention as the editor viewports (EditorCamera): Vulkan clip space,
    // Y flipped, reverse-less 0..1 depth.
    Mat4 MakeVulkanPerspective(float fovYRadians, float aspect, float nearPlane, float farPlane)
    {
        const float tanHalfFov = std::tan(fovYRadians * 0.5f);
        Mat4 result;
        result[0][0] = 1.0f / (aspect * tanHalfFov);
        result[1][1] = -1.0f / tanHalfFov;
        result[2][2] = farPlane / (nearPlane - farPlane);
        result[2][3] = (farPlane * nearPlane) / (nearPlane - farPlane);
        result[3][2] = -1.0f;
        return result;
    }
}

MaterialPreviewRenderFeature::MaterialPreviewRenderFeature(RuntimeAssets& assets)
    : Assets(assets)
{
}

void MaterialPreviewRenderFeature::Setup(const RendererServices& services)
{
    Services = services;
    Targets.Setup(services);
    Forward.Setup(services);

    for (std::size_t i = 0; i < Meshes.size(); ++i)
    {
        const auto kind = static_cast<PreviewPrimitive>(i);
        Meshes[i] = Assets.StaticMeshes.CreateFromData(PreviewPrimitiveName(kind),
                                                       BuildPreviewPrimitive(kind));
    }
}

void MaterialPreviewRenderFeature::Teardown()
{
    Forward.Teardown();
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

void MaterialPreviewRenderFeature::OnDraw(const FrameContext& frame)
{
    Targets.BeginFrame(frame.FrameInFlightIndex);
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

    VkRenderingAttachmentInfo colorAttach{};
    colorAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttach.imageView = target->ColorView;
    colorAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.clearValue.color = { { 0.05f, 0.09f, 0.12f, 1.0f } };

    VkRenderingAttachmentInfo depthAttach{};
    depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttach.imageView = target->DepthView;
    depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea.offset = { 0, 0 };
    info.renderArea.extent = target->Extent;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colorAttach;
    info.pDepthAttachment = &depthAttach;
    vkCmdBeginRendering(frame.Cmd, &info);

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

        RenderQueueItem item;
        item.Mesh = Meshes[static_cast<std::size_t>(Active)];
        item.Material = Material;
        item.SectionIndex = 0;
        item.WorldMatrix = Mat4::Identity();
        Queue.Reset();
        Queue.AddOpaque(item);
        Queue.SortOpaque();

        FrameContext local{};
        local.Cmd = frame.Cmd;
        local.FrameInFlightIndex = frame.FrameInFlightIndex;
        local.TargetExtent = target->Extent;
        local.TargetFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        local.DepthView = target->DepthView;
        local.DepthFormat = Services.DepthFormat;
        local.Phase = RenderPhase::Offscreen;

        Forward.Draw(local, camera, Lights, Queue, Assets.StaticMeshes, Assets.Materials);
    }

    vkCmdEndRendering(frame.Cmd);

    transitionColor(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    *target->ColorLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
