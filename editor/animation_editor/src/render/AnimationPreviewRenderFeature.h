#pragma once

#include "AnimationPreviewScene.h"
#include "render/ImGuiTargetPresenter.h"
#include "viewport/OrbitCamera.h"

#include <graphics/vulkan/RenderTargetStore.h>
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
    [[nodiscard]] ImTextureID Display(VkExtent2D extent);
    void Orbit(float yaw, float pitch);
    void Zoom(float delta);
    void FrameSubject();

private:
    RuntimeAssets& Assets;
    AnimationPreviewScene& Scene;
    SkinnedPoseRenderFeature Skinning;
    RenderTargetStore Targets;
    ImGuiTargetPresenter Presenter;
    RenderTargetId Target;
    LightBindings Lighting;
    MeshForwardPass Forward;
    RenderLightSet Lights;
    RendererServices Services{};
    OrbitCamera Camera;
    float Radius = 1.0f;
};
