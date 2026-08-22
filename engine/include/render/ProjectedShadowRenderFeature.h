#pragma once

#include <graphics/vulkan/ProjectedShadowProjectPass.h>
#include <graphics/vulkan/ProjectedShadowSilhouettePass.h>
#include <graphics/vulkan/Renderer.h>
#include <render/Camera.h>
#include <render/ProjectedShadowFramePolicy.h>
#include <render/ProjectedShadowTypes.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <profiling/RenderInstrumentation.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <memory>
#include <vector>

//=============================================================================
// ProjectedShadowRenderFeature
//
// The game's projected-object-shadow orchestration: in the Offscreen phase it
// ranks the frame's casters against the camera, renders each one's silhouette
// tile, and publishes the fully-baked projection inputs -- including the one
// game view's camera matrix and scissor -- for MeshRenderFeature to apply
// between its opaque and transparent halves.
//
// Bridge type: holds the silhouette pass by value, so this header includes a
// backend header beyond Renderer.h and is on the isolation check's header
// allowlist (the SkyRenderFeature precedent). The recording stayed in the
// backend, which is why the recording set did not grow.
//=============================================================================
class ProjectedShadowRenderFeature : public IRenderFeature
{
public:
    ProjectedShadowRenderFeature(ProjectedShadowSet& casters,
                                 const RenderLightSet& lights,
                                 const RenderQueue& queue,
                                 const CameraRenderData& camera,
                                 StaticMeshCache& meshes,
                                 const SkinnedMeshCache& skinnedMeshes,
                                 const ProjectedShadowBudgets& budgets,
                                 std::shared_ptr<ProjectedShadowFrameData> output);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    [[nodiscard]] bool Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    ProjectedShadowSet* Casters = nullptr;
    const RenderLightSet* Lights = nullptr;
    const RenderQueue* Queue = nullptr;
    const CameraRenderData* Camera = nullptr;
    StaticMeshCache* Meshes = nullptr;
    const SkinnedMeshCache* SkinnedMeshes = nullptr;
    const ProjectedShadowBudgets* Budgets = nullptr;
    std::shared_ptr<ProjectedShadowFrameData> Output;
    const RenderInstrumentation* Instrumentation = nullptr;

    ProjectedShadowSilhouettePass Silhouettes;

    // Assembly scratch, retained across frames.
    std::vector<ProjectedSilhouetteCasterDraw> CasterDraws;
    std::vector<ProjectedSilhouetteSectionDraw> SectionDraws;
    std::vector<std::uint32_t> ReceiverIndices;
    std::vector<ProjectedShadowScreenRect> UnionScratch;
};
