#include "AnimationPreviewRenderFeature.h"

#include <assets/runtime/RuntimeAssets.h>
#include <render/CameraProjection.h>

#include <algorithm>
#include <cmath>

AnimationPreviewRenderFeature::AnimationPreviewRenderFeature(
    RuntimeAssets& assets, AnimationPreviewScene& scene)
    : Assets(assets), Scene(scene), Skinning(scene.Poses, *assets.SkinnedMeshes)
{
}

bool AnimationPreviewRenderFeature::Setup(const RenderFeatureServices& services)
{
    Services = *services.Backend;
    Picture.Setup(Services, "animation_preview");
    if (!Lighting.Setup(Services) || !Skinning.Setup(services))
        return false;
    Forward.Setup(Services, Lighting);
    WideLines.Setup(Services);
    Forward.SetSkinnedPoses(Scene.Poses.get());
    return true;
}

void AnimationPreviewRenderFeature::Teardown()
{
    WideLines.Teardown();
    Forward.Teardown();
    Skinning.Teardown();
    Lighting.Teardown();
    Picture.Teardown();
}

void AnimationPreviewRenderFeature::Show(VkExtent2D extent, UiHostImageId image)
{
    Picture.Show(extent, image);
}

void AnimationPreviewRenderFeature::Orbit(float yaw, float pitch)
{
    Camera.Orbit(yaw, pitch);
}

void AnimationPreviewRenderFeature::Zoom(float delta)
{
    Camera.Zoom(delta);
}

bool AnimationPreviewRenderFeature::FrameSubject()
{
    if (!Scene.Bounds.IsValid())
        return false;
    Radius = std::max(0.01f, Scene.Bounds.HalfExtent().Magnitude());
    Camera.Frame(Scene.Bounds.Center(), Radius);
    return true;
}

void AnimationPreviewRenderFeature::OnDraw(const RenderFrame& renderFrame)
{
    const FrameContext& frame = *renderFrame.Backend;
    const std::optional<RenderTargetView> target = Picture.Begin(frame);
    if (!target)
        return;
    // Dispatch before opening dynamic rendering, using the runtime pose pass.
    Skinning.OnDraw(renderFrame);
    Picture.Draw(frame, *target, { { 0.025f, 0.03f, 0.045f, 1.0f } },
                 [&](const RenderScope& rendering) { Record(rendering, target->Extent); });
}

void AnimationPreviewRenderFeature::Record(const RenderScope& rendering, VkExtent2D extent)
{
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const CameraRenderData camera = Camera.BuildRenderData(aspect);
    const Vec3d eye = camera.Position;
    Lights.Reset();
    PointLightComponent light;
    light.Intensity = 8.0f * Radius * Radius;
    light.Range = Radius * 30.0f;
    Lights.AddPoint(eye + Vec3d(0.0f, Radius, 0.0f), light);
    Forward.Draw(rendering.Context(), MeshForwardPass::DrawContext{
        .Camera = camera, .Lights = Lights, .Queue = Scene.Queue,
        .Meshes = *Assets.StaticMeshes, .Materials = Assets.Materials,
        .SkinnedMeshes = Assets.SkinnedMeshes.get() });
    WideLines.Submit(rendering.Context(),
                     Rect2d(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height)),
                     camera, Strokes, /*onTop*/ true, "AnimationPreview.strokes");
}
