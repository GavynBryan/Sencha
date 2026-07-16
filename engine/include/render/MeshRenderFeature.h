#pragma once

#include <render/MeshForwardPass.h>

#include <memory>

//=============================================================================
// MeshRenderFeature
//
// IRenderFeature that draws all opaque meshes in the RenderQueue using the
// mesh_forward shader. Runs in RenderPhase::MainColor. A thin wrapper that
// holds the game's queue/caches/camera and drives a MeshForwardPass; the draw
// itself lives in the pass so the editor can reuse it. The lighting bindings
// are shared with the SpotShadowRenderFeature that renders the atlas; that
// feature's Setup must run first so the descriptor set layout exists.
//=============================================================================
class MeshRenderFeature : public IRenderFeature
{
public:
    MeshRenderFeature(RenderQueue& queue,
                      StaticMeshCache& meshes,
                      MaterialCache& materials,
                      const CameraRenderData& camera,
                      const RenderLightSet& lights,
                      std::shared_ptr<LightBindings> bindings);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    RenderQueue* Queue = nullptr;
    StaticMeshCache* Meshes = nullptr;
    MaterialCache* Materials = nullptr;
    const CameraRenderData* Camera = nullptr;
    const RenderLightSet* Lights = nullptr;
    std::shared_ptr<LightBindings> Bindings;
    MeshForwardPass Pass;
};
