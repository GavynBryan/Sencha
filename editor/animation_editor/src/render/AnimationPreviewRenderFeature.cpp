#include "AnimationPreviewRenderFeature.h"

#include <assets/runtime/RuntimeAssets.h>
#include <graphics/vulkan/RenderScope.h>
#include <graphics/vulkan/RenderTargetSession.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <render/CameraProjection.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr VkFormat ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
}

AnimationPreviewRenderFeature::AnimationPreviewRenderFeature(
    RuntimeAssets& assets, AnimationPreviewScene& scene)
    : Assets(assets), Scene(scene), Skinning(scene.Poses, *assets.SkinnedMeshes)
{
}

bool AnimationPreviewRenderFeature::Setup(const RenderFeatureServices& services)
{
    Services = *services.Backend;
    Targets.Setup(Services);
    if (Services.Samplers)
        Presenter.Setup(Services.Samplers->GetLinearClamp());
    RenderTargetDesc desc{};
    desc.ColorFormat = ColorFormat;
    desc.DepthFormat = Services.DepthFormat;
    desc.Read = RenderTargetRead::Sampled;
    desc.DebugName = "animation_preview";
    Target = Targets.Create(desc);
    if (!Lighting.Setup(Services) || !Skinning.Setup(services))
        return false;
    Forward.Setup(Services, Lighting);
    Forward.SetSkinnedPoses(Scene.Poses.get());
    return true;
}

void AnimationPreviewRenderFeature::Teardown()
{
    Forward.Teardown();
    Skinning.Teardown();
    Lighting.Teardown();
    Presenter.Release(Target);
    Targets.Teardown();
    Presenter.Teardown();
    Target = {};
}

ImTextureID AnimationPreviewRenderFeature::Display(VkExtent2D extent)
{
    Targets.SetExtent(Target, extent);
    return Presenter.Present(Targets, Target);
}

void AnimationPreviewRenderFeature::Orbit(float yaw, float pitch)
{
    Yaw += yaw;
    Pitch = std::clamp(Pitch + pitch, -1.5f, 1.5f);
}

void AnimationPreviewRenderFeature::Zoom(float delta)
{
    Distance = std::clamp(Distance * std::exp(-delta * 0.1f), Radius * 0.1f, Radius * 100.0f);
}

void AnimationPreviewRenderFeature::FrameSubject()
{
    if (!Scene.Bounds.IsValid())
        return;
    Center = Scene.Bounds.Center();
    Radius = std::max(0.01f, Scene.Bounds.HalfExtent().Magnitude());
    Distance = Radius * 3.0f;
}

void AnimationPreviewRenderFeature::OnDraw(const RenderFrame& renderFrame)
{
    const auto& frame = *renderFrame.Backend;
    Targets.BeginFrame(frame.FrameInFlightIndex);
    Presenter.BeginFrame(frame.Retirement);
    const auto target = Targets.Acquire(Target);
    if (!target)
        return;
    // Dispatch before opening dynamic rendering, using the runtime pose pass.
    Skinning.OnDraw(renderFrame);
    RenderTargetSession session(frame.Cmd, target->ColorImage, target->ColorLayout, target->DepthImage);
    RenderScopeDesc desc{};
    desc.Area.extent = target->Extent;
    desc.Color.View = target->ColorView;
    desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    desc.Color.Clear.color = { { 0.025f, 0.03f, 0.045f, 1.0f } };
    desc.ColorFormat = ColorFormat;
    desc.Depth.View = target->DepthView;
    desc.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    desc.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    desc.Depth.Clear.depthStencil = { 1.0f, 0 };
    desc.DepthFormat = Services.DepthFormat;
    desc.Phase = RenderPhase::Offscreen;
    const RenderScope rendering(frame, desc);

    const Vec3d eye = Center + Vec3d(Distance * std::cos(Pitch) * std::sin(Yaw),
                                    Distance * std::sin(Pitch),
                                    Distance * std::cos(Pitch) * std::cos(Yaw));
    CameraRenderData camera;
    camera.Position = eye;
    camera.View = Mat4::MakeLookAt(eye, Center, Vec3d(0.0f, 1.0f, 0.0f));
    const float aspect = static_cast<float>(target->Extent.width)
                       / static_cast<float>(target->Extent.height);
    camera.Projection = MakeVulkanPerspective(0.9f, aspect, Radius * 0.01f, Radius * 200.0f);
    camera.ViewProjection = camera.Projection * camera.View;
    camera.ViewFrustum = Frustum::FromViewProjection(camera.ViewProjection);
    Lights.Reset();
    PointLightComponent light;
    light.Intensity = 8.0f * Radius * Radius;
    light.Range = Radius * 30.0f;
    Lights.AddPoint(eye + Vec3d(0.0f, Radius, 0.0f), light);
    Forward.Draw(rendering.Context(), MeshForwardPass::DrawContext{
        .Camera = camera, .Lights = Lights, .Queue = Scene.Queue,
        .Meshes = *Assets.StaticMeshes, .Materials = Assets.Materials,
        .SkinnedMeshes = Assets.SkinnedMeshes.get() });
}
