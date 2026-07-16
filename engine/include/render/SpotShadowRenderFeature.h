#pragma once

#include <graphics/vulkan/Renderer.h>
#include <render/LightBindings.h>
#include <render/RenderLight.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidency.h>
#include <render/SpotShadowDepthPass.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <memory>

//=============================================================================
// SpotShadowRenderFeature
//
// IRenderFeature that owns the lighting bindings' lifetime for the game
// renderer and records the residency arbiter's scheduled shadow views each
// frame through SpotShadowDepthPass. Runs in Offscreen so tiles are written
// before the MainColor forward pass samples them. The bindings are shared
// with MeshRenderFeature, whose Setup must run after this feature's so the
// set layout exists when the forward pipeline layout is created.
//=============================================================================
class SpotShadowRenderFeature final : public IRenderFeature
{
public:
    SpotShadowRenderFeature(std::shared_ptr<LightBindings> bindings,
                            const RenderLightSet& lights,
                            const ShadowCasterSet& casters,
                            StaticMeshCache& meshes,
                            ShadowResidency& residency);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    std::shared_ptr<LightBindings> Bindings;
    const RenderLightSet& Lights;
    const ShadowCasterSet& Casters;
    StaticMeshCache& Meshes;
    ShadowResidency& Residency;
    const RenderInstrumentation* Instrumentation = nullptr;
    SpotShadowDepthPass Pass;
};
