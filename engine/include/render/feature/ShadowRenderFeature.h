#pragma once

#include <graphics/RenderFeature.h>
#include <render/pass/LightBindings.h>
#include <render/RenderLight.h>
#include <render/ShadowCasterSet.h>
#include <render/pass/ShadowDepthPass.h>
#include <render/ShadowResidency.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <memory>

//=============================================================================
// ShadowRenderFeature
//
// IRenderFeature that owns the lighting bindings' lifetime for the game
// renderer and records the residency arbiter's scheduled shadow views each
// frame through ShadowDepthPass. Runs in Offscreen so the atlas and cube
// pool are written before the MainColor forward pass samples them. The
// bindings are shared with MeshRenderFeature, whose Setup must run after
// this feature's so the set layout exists when the forward pipeline layout
// is created.
//=============================================================================
class ShadowRenderFeature final : public IRenderFeature
{
public:
    ShadowRenderFeature(std::shared_ptr<LightBindings> bindings,
                        RenderLightSet& lights,
                        const ShadowCasterSet& casters,
                        StaticMeshCache& meshes,
                        ShadowResidency& residency);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    [[nodiscard]] bool Setup(const RenderFeatureServices& services) override;
    void OnDraw(const RenderFrame& frame) override;
    void Teardown() override;

private:
    std::shared_ptr<LightBindings> Bindings;
    RenderLightSet& Lights;
    const ShadowCasterSet& Casters;
    StaticMeshCache& Meshes;
    ShadowResidency& Residency;
    const RenderInstrumentation* Instrumentation = nullptr;
    ShadowDepthPass Pass;
};
