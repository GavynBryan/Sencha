#pragma once

#include <graphics/vulkan/MeshDrawSubmitter.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/pass/LightBindings.h>
#include <render/RenderLight.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidency.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cstddef>
#include <span>
#include <vector>

// Depth bias is baked into the pipeline rather than set dynamically, so the
// family is compiled against the light set's values and recompiled when a cvar
// changes them. Compared exactly: a different bias is a different pipeline.
struct ShadowDepthBias
{
    float Constant = 0.0f;
    float Slope = 0.0f;

    friend bool operator==(ShadowDepthBias, ShadowDepthBias) = default;
};

// Face-culling variants of the depth-only pipeline. Casters differ only in
// which faces they cull, so the shading state is identical across all three.
enum class ShadowPipelineId : std::size_t
{
    Back = 0,
    // Cube face projections mirror winding, so a point face culls back faces
    // with the opposite front-face state.
    FlippedBack = 1,
    DoubleSided = 2,
};

inline constexpr std::size_t kShadowPipelineCount = 3;

[[nodiscard]] constexpr ShadowPipelineId SelectShadowPipeline(bool doubleSided,
                                                              bool flipFrontFace)
{
    if (doubleSided)
        return ShadowPipelineId::DoubleSided;
    return flipFrontFace ? ShadowPipelineId::FlippedBack : ShadowPipelineId::Back;
}

//=============================================================================
// ShadowDepthPass
//
// Records the residency arbiter's scheduled shadow views: spot tiles into
// the atlas and point faces into the cube pool, one depth-only
// dynamic-rendering scope per view, casters frustum-culled per view and
// drawn through an instance transform stream. Factored like MeshForwardPass
// so the editor drives the identical depth path for its viewports; the game
// wraps it in ShadowRenderFeature. Depth-bias pipelines follow the light
// set's bias values; point faces render with flipped front-face state
// because the cube face projection mirrors winding. A view whose recording
// cannot proceed is skipped before its target is touched and reported to
// the residency arbiter (when given one) so cached content survives and the
// view re-queues; its current-frame light grant is revoked so the forward
// pass never samples that content against a record it was not rendered with.
//=============================================================================
class ShadowDepthPass
{
public:
    void Setup(const RendererServices& services, LightBindings& bindings);
    // Everything one Draw call reads. `Residency`, when given, hears about
    // views the pass had to abandon so cached content survives and the view
    // re-queues.
    struct DrawContext
    {
        RenderLightSet& Lights;
        std::span<const SpotShadowViewJob> Views;
        std::span<const PointShadowFaceJob> PointFaces;
        const ShadowCasterSet& Casters;
        StaticMeshCache& Meshes;
        ShadowResidency* Residency = nullptr;
    };
    void Draw(const FrameContext& frame, const DrawContext& ctx);
    void Teardown();

    // Pass-local totals at view/draw granularity, maintained unconditionally
    // and copied into RenderStats by the owning feature when counters run.
    struct DrawStats
    {
        std::uint32_t ViewsRendered = 0;
        std::uint32_t PointFacesRendered = 0;
        // Casters covered by the emitted draws, summed over views. A caster
        // drawn into six cube faces counts six times.
        std::uint32_t CasterDraws = 0;
        // Frustum tests across every rendered view, and the survivors. Both
        // accumulate over views, so a caster tested by six cube faces counts
        // six times: the pair measures how much work culling is avoiding.
        std::uint32_t CastersTested = 0;
        std::uint32_t CastersVisible = 0;
        // Casters the frame scratch could not carry, summed over views.
        std::uint32_t CastersDropped = 0;
        // Draw calls emitted. Casters sharing a pipeline, mesh, and section
        // collapse into one, so CasterDraws divided by this is the batching
        // factor -- equal values mean nothing merged.
        std::uint32_t InstanceRuns = 0;
        // Set when the pass had views to render and abandoned all of them
        // (missing pipelines, or a frame-scratch request it could not serve).
        bool Skipped = false;
    };
    [[nodiscard]] DrawStats GetLastDrawStats() const { return LastStats; }

private:
    struct ViewTarget
    {
        VkImageView Attachment = VK_NULL_HANDLE;
        VkRect2D RenderArea{};
        VkViewport Viewport{};
    };

    [[nodiscard]] bool EnsurePipelines(const RenderLightSet& lights);
    // Fills VisibleCasters with the casters this view can see, in draw-run
    // order. `lightSphere` (xyz = position, w = range) rejects casters the
    // light cannot reach at all before the frustum test; null skips it.
    void GatherVisibleCasters(const Mat4& viewProjection,
                              const ShadowCasterSet& casters,
                              StaticMeshCache& meshes,
                              const Vec4* lightSphere);
    [[nodiscard]] VkDeviceSize UploadView(const Mat4& viewProjection);
    void BindView(const FrameContext& frame, VkDeviceSize uniformOffset);
    // Returns false only when the view uniform cannot be uploaded; the
    // target has not been touched.
    bool RecordView(const FrameContext& frame,
                    const ViewTarget& target,
                    const Mat4& viewProjection,
                    const ShadowCasterSet& casters,
                    StaticMeshCache& meshes,
                    const Vec4* lightSphere,
                    bool flipFrontFace);

    LightBindings* Bindings = nullptr;
    VulkanBufferService* Buffers = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    GpuFrameScratch* Scratch = nullptr;
    VulkanPipelineCache* PipelineCache = nullptr;
    VulkanShaderCache* Shaders = nullptr;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    PipelineVariantSet<kShadowPipelineCount, ShadowDepthBias> Pipelines;
    MeshDrawSubmitter Submitter;
    DrawStats LastStats;

    // Per-view visible set, in draw-run order. Held across frames so a view
    // walk does not allocate.
    std::vector<std::uint32_t> VisibleCasters;
};
