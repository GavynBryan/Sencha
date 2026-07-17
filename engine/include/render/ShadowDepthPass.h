#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/LightBindings.h>
#include <render/RenderLight.h>
#include <render/ShadowCasterSet.h>
#include <render/ShadowResidency.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <span>

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
    void Draw(const FrameContext& frame,
              RenderLightSet& lights,
              std::span<const SpotShadowViewJob> views,
              std::span<const PointShadowFaceJob> pointFaces,
              const ShadowCasterSet& casters,
              StaticMeshCache& meshes,
              ShadowResidency* residency);
    void Teardown();

    // Pass-local totals at view/draw granularity, maintained unconditionally
    // and copied into RenderStats by the owning feature when counters run.
    struct DrawStats
    {
        std::uint32_t ViewsRendered = 0;
        std::uint32_t PointFacesRendered = 0;
        std::uint32_t CasterDraws = 0;
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
    [[nodiscard]] bool BindInstanceStream(const FrameContext& frame,
                                          const ShadowCasterSet& casters);
    [[nodiscard]] VkDeviceSize UploadView(const Mat4& viewProjection);
    void BindView(const FrameContext& frame, VkDeviceSize uniformOffset);
    // Returns false only when the view uniform cannot be uploaded; the
    // target has not been touched.
    bool RecordView(const FrameContext& frame,
                    const ViewTarget& target,
                    const Mat4& viewProjection,
                    const ShadowCasterSet& casters,
                    StaticMeshCache& meshes,
                    bool canDrawCasters,
                    bool flipFrontFace);

    LightBindings* Bindings = nullptr;
    VulkanBufferService* Buffers = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VulkanPipelineCache* PipelineCache = nullptr;
    VulkanShaderCache* Shaders = nullptr;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline BackPipeline = VK_NULL_HANDLE;
    // The unflipped cube face projection mirrors winding, so point faces
    // cull back faces with the opposite front-face state.
    VkPipeline FlippedBackPipeline = VK_NULL_HANDLE;
    VkPipeline DoubleSidedPipeline = VK_NULL_HANDLE;
    float CachedBiasConstant = -1.0f;
    float CachedBiasSlope = -1.0f;
    DrawStats LastStats;

    // Bind-state dedup across the views of one Draw.
    VkPipeline LastPipeline = VK_NULL_HANDLE;
    VkBuffer LastVertexBuffer = VK_NULL_HANDLE;
    VkBuffer LastIndexBuffer = VK_NULL_HANDLE;
};
