#pragma once

#include "viewport/OrbitCamera.h"

#include "PreviewBackdropRenderer.h"
#include "PreviewPrimitives.h"

#include "render/DisplayedTargets.h"

#include <graphics/vulkan/Renderer.h>
#include <render/pass/MeshForwardPass.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/ui/UiDrawFrame.h>

#include <array>
#include <optional>

struct RuntimeAssets;

//=============================================================================
// MaterialPreviewRenderFeature
//
// The material editor's single offscreen view: one procedural primitive, one
// material, one point light plus hemispheric ambient, drawn through the
// runtime MeshForwardPass into an offscreen target a host image shows. Orbit
// state lives here; the workspace feeds it pointer deltas.
//=============================================================================
class MaterialPreviewRenderFeature : public IRenderFeature
{
public:
    explicit MaterialPreviewRenderFeature(RuntimeAssets& assets);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

    // UI side: the size it is shown at, and the host image that shows it.
    void Show(VkExtent2D extent, UiHostImageId image);

    void SetMaterial(MaterialHandle material) { Material = material; }
    void SetPrimitive(PreviewPrimitive primitive) { Active = primitive; }
    [[nodiscard]] PreviewPrimitive GetPrimitive() const { return Active; }

    void Orbit(float yawDelta, float pitchDelta);
    void Zoom(float wheelDelta);

    float LightIntensity = 8.0f;

    // Backdrop look, refreshed per frame from the editor.preview.backdrop.*
    // cvars by the composition root.
    PreviewBackdropStyle BackdropStyle;

private:
    RuntimeAssets& Assets;
    DisplayedTargets Targets;
    RenderTargetId SceneTarget;
    std::optional<UiHostImageId> Image;
    PreviewBackdropRenderer Backdrop;
    // The forward pass requires the lighting bindings for its descriptor
    // layout. The preview never renders shadow tiles, so it skips atlas
    // creation entirely: the set stays dummy-backed and the preview light
    // set carries a zero spot-shadow count.
    LightBindings Lighting;
    MeshForwardPass Forward;
    RenderQueue Queue;
    RenderLightSet Lights;
    RendererServices Services{};

    std::array<StaticMeshHandle, static_cast<std::size_t>(PreviewPrimitive::Count)> Meshes{};
    PreviewPrimitive Active = PreviewPrimitive::Sphere;
    MaterialHandle Material{};

    // A material sphere at the origin, turned over by the author. The zoom and
    // depth range are the sphere's, not a framed subject's.
    OrbitCamera Camera{ .Target = {}, .Yaw = 0.6f, .Pitch = 0.35f, .Distance = 1.6f,
                        .MinDistance = 0.6f, .MaxDistance = 8.0f, .FovYRadians = 0.9f,
                        .Near = 0.05f, .Far = 50.0f };
};
