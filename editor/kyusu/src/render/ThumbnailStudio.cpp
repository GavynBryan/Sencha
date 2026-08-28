#include "ThumbnailStudio.h"

#include <graphics/FramesInFlight.h>
#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/SkyGradientPass.h>
#include <math/geometry/3d/Aabb3d.h>
#include <render/CameraProjection.h>
#include <render/pass/MeshForwardPass.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr VkExtent2D kThumbnailExtent{ 256, 256 };
    constexpr std::uint32_t kThumbnailIdBase = 0x40000000;
}

ThumbnailStudio::ThumbnailStudio(ViewportTargetCache& targets,
                                 MeshForwardPass& forward,
                                 SkyGradientPass& sky,
                                 StaticMeshCache& meshes,
                                 MaterialCache& materials,
                                 const SkinnedMeshCache* skinnedMeshes,
                                 VkFormat depthFormat)
    : Targets(targets)
    , Forward(forward)
    , Sky(sky)
    , Meshes(meshes)
    , Materials(materials)
    , SkinnedMeshes(skinnedMeshes)
    , DepthFormat(depthFormat)
{
}

ViewportId ThumbnailStudio::AllocateTarget()
{
    return ViewportId{ kThumbnailIdBase + NextTargetId++ };
}

ImTextureID ThumbnailStudio::Display(ViewportId target)
{
    return Targets.Display(target, kThumbnailExtent);
}

int ThumbnailStudio::PassesPerTarget()
{
    return static_cast<int>(kMaxFramesInFlight);
}

CameraRenderData ThumbnailStudio::FrameSubject(const Aabb3d& bounds)
{
    constexpr float kFovY = 35.0f * 3.14159265f / 180.0f;
    const float tanHalf = std::tan(kFovY * 0.5f);
    const Vec3d center = bounds.Center();
    const Vec3d half = (bounds.Max - bounds.Min) * 0.5f;

    const Vec3d direction = Vec3d(1.0f, 0.75f, 1.0f).Normalized();
    const Vec3d forward = direction * -1.0f; // eye looks back at the center
    const Vec3d right = forward.Cross(Vec3d::Up()).Normalized();
    const Vec3d up = right.Cross(forward).Normalized();

    // For each corner: how far back the eye must sit for that corner to stay
    // inside the square frustum, accounting for its own depth.
    float distance = 0.5f;
    for (int i = 0; i < 8; ++i)
    {
        const Vec3d corner((i & 1) != 0 ? half.X : -half.X,
                           (i & 2) != 0 ? half.Y : -half.Y,
                           (i & 4) != 0 ? half.Z : -half.Z);
        const float depth = corner.Dot(forward);
        const float lateral = std::max(std::abs(corner.Dot(right)),
                                       std::abs(corner.Dot(up)));
        distance = std::max(distance, lateral / tanHalf - depth);
    }
    distance *= 1.08f; // breathing room at the cell edge

    const Vec3d eye = center + direction * distance;
    const float radius = std::max(
        0.25f, std::sqrt(half.X * half.X + half.Y * half.Y + half.Z * half.Z));

    CameraRenderData camera;
    camera.Position = eye;
    camera.View = Mat4::MakeLookAt(eye, center, Vec3d::Up());
    camera.Projection = MakeVulkanPerspective(
        kFovY, 1.0f, std::max(0.01f, (distance - radius) * 0.5f),
        distance + radius * 4.0f);
    camera.ViewProjection = camera.Projection * camera.View;
    return camera;
}

bool ThumbnailStudio::RenderPass(const FrameContext& frame, ViewportId target,
                                 const CameraRenderData& camera,
                                 std::span<const RenderQueue* const> queues)
{
    const std::optional<ViewportTargetCache::RenderView> view =
        Targets.AcquireForRender(target);
    if (!view.has_value())
        return false;

    RenderTargetSession session(frame.Cmd, view->ColorImage, view->ColorLayout,
                                view->DepthImage);
    RenderScopeDesc scope{};
    scope.Area.offset = { 0, 0 };
    scope.Area.extent = view->Extent;
    scope.Color.View = view->ColorView;
    scope.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Color.Clear.color = { { 0.05f, 0.09f, 0.12f, 1.0f } };
    scope.ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    scope.Depth.View = view->DepthView;
    scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    scope.Depth.Clear.depthStencil = { 1.0f, 0 };
    scope.DepthFormat = DepthFormat;
    scope.Phase = RenderPhase::Offscreen;

    {
        const RenderScope rendering(frame, scope);
        const FrameContext& local = rendering.Context();

        // Studio look: sky gradient behind, full-bright neutral ambient over
        // the geometry -- readable materials with no dependence on any scene's
        // lights or shadows.
        RenderLightSet lights;
        lights.AmbientSky = Vec<3>(1.0f, 1.0f, 1.0f);
        lights.AmbientGround = Vec<3>(0.75f, 0.75f, 0.75f);
        Sky.Draw(local,
                 MakeInverseSkyViewProjection(camera.View, camera.Projection),
                 SkyGradientParams{ .Top = Vec<3>(0.16f, 0.22f, 0.28f),
                                    .Bottom = Vec<3>(0.07f, 0.09f, 0.11f),
                                    .Exposure = 1.0f,
                                    .TonemapKnee = 1.0f,
                                    .TonemapEnabled = false });
        for (const RenderQueue* queue : queues)
        {
            if (queue == nullptr)
                continue;
            Forward.Draw(local, MeshForwardPass::DrawContext{
                                    .Camera = camera,
                                    .Lights = lights,
                                    .Queue = *queue,
                                    .Meshes = Meshes,
                                    .Materials = Materials,
                                    .SkinnedMeshes = SkinnedMeshes });
        }
    }
    session.End();
    return true;
}
