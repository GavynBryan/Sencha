#pragma once

#include <graphics/RenderFeature.h>
#include <graphics/vulkan/OutlineMaskTargets.h>
#include <render/outline/OutlineFrame.h>
#include <render/pass/MeshForwardPass.h>

// Draws the outline mask for the runtime, between the scene and the effect chain.
// Records nothing and holds no mask image while nothing is highlighted.
class OutlineRenderFeature final : public IRenderFeature
{
public:
    // `forward` belongs to the mesh feature this declares a dependency on.
    OutlineRenderFeature(MeshForwardPass& forward, OutlineFrame& outlines, const CameraRenderData& camera,
                         const RenderLightSet& lights, StaticMeshCache& meshes, MaterialCache& materials,
                         const SkinnedMeshCache* skinnedMeshes);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Post; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

    [[nodiscard]] bool HoldsMaskImage() { return Targets.HoldsMaskImage(); }

private:
    MeshForwardPass* Forward = nullptr;
    OutlineFrame* Outlines = nullptr;
    const CameraRenderData* Camera = nullptr;
    const RenderLightSet* Lights = nullptr;
    StaticMeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
    const SkinnedMeshCache* SkinnedMeshes = nullptr;
    OutlineMaskTargets Targets;
};
