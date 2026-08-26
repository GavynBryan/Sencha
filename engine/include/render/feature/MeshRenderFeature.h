#pragma once

#include <render/pass/MeshForwardPass.h>
#include <render/ProbeVolumeSet.h>
#include <render/SkinnedPoseFrameData.h>

#include <memory>

//=============================================================================
// MeshRenderFeature
//
// IRenderFeature that draws all opaque meshes in the RenderQueue using the
// mesh_forward shader. Runs in RenderPhase::MainColor. A thin wrapper that
// holds the game's queue/caches/camera and drives a MeshForwardPass; the draw
// itself lives in the pass so the editor can reuse it. The lighting bindings
// are shared with the ShadowRenderFeature that renders the shadow targets; that
// feature's Setup must run first so the descriptor set layout exists.
//=============================================================================
class MeshRenderFeature : public IRenderFeature
{
public:
    // `skinnedPoses` carries the frame's posed skinned geometry (produced by
    // SkinnedPoseRenderFeature in the Offscreen phase). Null draws skinned
    // items at rest.
    //
    // `probes` is the residency this feature's pass samples through binding 2.
    // The feature drives its frame clock because reclaiming a probe slot has
    // to happen against the frames that actually submitted, and the render
    // phase is the only place that number is current -- see
    // ProbeVolumeSet::BeginFrame. Null for a host with no probe residency.
    MeshRenderFeature(RenderQueue& queue,
                      StaticMeshCache& meshes,
                      MaterialCache& materials,
                      const CameraRenderData& camera,
                      const RenderLightSet& lights,
                      std::shared_ptr<LightBindings> bindings,
                      const SkinnedMeshCache* skinnedMeshes = nullptr,
                      std::shared_ptr<const SkinnedPoseFrameData> skinnedPoses = nullptr,
                      ProbeVolumeSet* probes = nullptr);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

private:
    RenderQueue* Queue = nullptr;
    StaticMeshCache* Meshes = nullptr;
    const SkinnedMeshCache* SkinnedMeshes = nullptr;
    MaterialCache* Materials = nullptr;
    const CameraRenderData* Camera = nullptr;
    const RenderLightSet* Lights = nullptr;
    std::shared_ptr<LightBindings> Bindings;
    std::shared_ptr<const SkinnedPoseFrameData> SkinnedPoses;
    ProbeVolumeSet* Probes = nullptr;
    const RenderInstrumentation* Instrumentation = nullptr;
    MeshForwardPass Pass;
};
