#pragma once

#include "AnimationPreviewScene.h"
#include "render/DisplayedTargets.h"
#include "render/EditorWideLinePipeline.h"
#include "viewport/OrbitCamera.h"

#include <render/ui/UiDrawFrame.h>

#include <optional>
#include <vector>

#include <render/feature/SkinnedPoseRenderFeature.h>
#include <render/pass/MeshForwardPass.h>

struct RuntimeAssets;

class AnimationPreviewRenderFeature : public IRenderFeature
{
public:
    AnimationPreviewRenderFeature(RuntimeAssets& assets, AnimationPreviewScene& scene);
    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;
    // Per frame it is seen: renders at `extent` into the host image named.
    void Show(VkExtent2D extent, UiHostImageId image);
    // Drawn over the pose, through it rather than behind it.
    void SetStrokes(std::vector<EditorLineSegment> strokes) { Strokes = std::move(strokes); }
    void Orbit(float yaw, float pitch);
    void Zoom(float delta);
    // False while there is nothing to frame.
    bool FrameSubject();
    [[nodiscard]] CameraRenderData ViewCamera(float aspect) const { return Camera.BuildRenderData(aspect); }

private:
    void Record(const FrameContext& frame, const RenderTargetView& target);

    RuntimeAssets& Assets;
    AnimationPreviewScene& Scene;
    SkinnedPoseRenderFeature Skinning;
    DisplayedTargets Targets;
    RenderTargetId Target;
    std::optional<UiHostImageId> Image;
    EditorWideLinePipeline WideLines;
    std::vector<EditorLineSegment> Strokes;
    LightBindings Lighting;
    MeshForwardPass Forward;
    RenderLightSet Lights;
    RendererServices Services{};
    OrbitCamera Camera;
    float Radius = 1.0f;
};
