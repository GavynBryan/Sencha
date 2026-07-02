#pragma once

#include "PreviewPrimitives.h"

#include "render/ViewportTargetCache.h"

#include <graphics/vulkan/Renderer.h>
#include <render/MeshForwardPass.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>

#include <array>

struct RuntimeAssets;

//=============================================================================
// MaterialPreviewRenderFeature
//
// The material editor's single offscreen view: one procedural primitive, one
// material, one point light plus hemispheric ambient, drawn through the
// runtime MeshForwardPass into a ViewportTargetCache slot the preview panel
// shows via ImGui::Image. Orbit state lives here; the panel feeds it mouse
// deltas.
//=============================================================================
class MaterialPreviewRenderFeature : public IRenderFeature
{
public:
    explicit MaterialPreviewRenderFeature(RuntimeAssets& assets);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

    // UI side: record the on-screen size, get the texture to display.
    [[nodiscard]] ImTextureID Display(VkExtent2D extent);

    void SetMaterial(MaterialHandle material) { Material = material; }
    void SetPrimitive(PreviewPrimitive primitive) { Active = primitive; }
    [[nodiscard]] PreviewPrimitive GetPrimitive() const { return Active; }

    // Drops the primitive meshes while the caches still live (the feature
    // itself tears down later, in ~Renderer). Call before the asset system
    // is released.
    void ReleaseResources();

    void Orbit(float yawDelta, float pitchDelta);
    void Zoom(float wheelDelta);

    float LightIntensity = 8.0f;

private:
    RuntimeAssets& Assets;
    ViewportTargetCache Targets;
    MeshForwardPass Forward;
    RenderQueue Queue;
    RenderLightSet Lights;
    RendererServices Services{};

    std::array<StaticMeshHandle, static_cast<std::size_t>(PreviewPrimitive::Count)> Meshes{};
    PreviewPrimitive Active = PreviewPrimitive::Sphere;
    MaterialHandle Material{};

    float Yaw = 0.6f;
    float Pitch = 0.35f;
    float Distance = 1.6f;
};
