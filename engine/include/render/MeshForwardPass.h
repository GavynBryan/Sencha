#pragma once

#include <graphics/vulkan/MeshDrawSubmitter.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/Camera.h>
#include <render/LightBindings.h>
#include <render/MaterialCache.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

struct GpuSpotShadow
{
    Mat4 ViewProjection;
    Vec4 AtlasScaleBias;
    Vec4 SamplingParams;
};

struct GpuPointShadow
{
    Vec4 PositionFar;
    Vec4 Params;
};

struct MeshFrameUniforms
{
    Mat4 ViewProjection;
    Vec4 ViewPositionTime;
    Vec4 AmbientSky;
    Vec4 AmbientGround;
    Vec4 StyleParams;
    std::uint32_t LightCount = 0;
    std::uint32_t TonemapEnabled = 1;
    float ShadowDarkness = 1.0f;
    std::uint32_t BakedDirectEnabled = 1;
    GpuLight Lights[kMaxForwardLights];
    std::uint32_t SpotShadowCount = 0;
    std::uint32_t BakedAoEnabled = 1;
    std::uint32_t ShadowPad1 = 0;
    std::uint32_t ShadowPad2 = 0;
    GpuSpotShadow SpotShadows[kMaxSpotShadows];
    std::uint32_t PointShadowCount = 0;
    std::uint32_t PointShadowPad0 = 0;
    std::uint32_t PointShadowPad1 = 0;
    std::uint32_t PointShadowPad2 = 0;
    GpuPointShadow PointShadows[kMaxPointShadows];
    std::uint32_t ProbeVolumeCount = 0;
    std::uint32_t ProbePad0 = 0;
    std::uint32_t ProbePad1 = 0;
    std::uint32_t ProbePad2 = 0;
    GpuProbeVolume ProbeVolumes[kMaxActiveProbeVolumes];
    std::uint32_t DebugView = 0;
    std::uint32_t DebugViewPad0 = 0;
    std::uint32_t DebugViewPad1 = 0;
    std::uint32_t DebugViewPad2 = 0;
};

// The transparent family varies over the opaque family's low two axes -- bit 0
// double-sided, bit 1 unlit -- and never the mask bit, since the alpha modes
// are exclusive. Folding an opaque id keeps the classifier in one place.
inline constexpr std::size_t kTransparentPipelineCount = 4;

[[nodiscard]] constexpr std::size_t TransparentPipelineIndex(OpaquePipelineId id)
{
    return static_cast<std::size_t>(static_cast<std::uint8_t>(id)
                                    & (kOpaquePipelineDoubleSidedBit | kOpaquePipelineUnlitBit));
}

// The debug and overdraw families vary over two of the opaque family's three
// axes: cull mode and alpha masking. Bit 0 double-sided, bit 1 masked.
inline constexpr std::size_t kDebugPipelineCount = 4;

// Folds an opaque pipeline id onto the debug families' narrower axes.
[[nodiscard]] constexpr std::size_t DebugPipelineIndex(OpaquePipelineId id)
{
    const auto bits = static_cast<std::uint8_t>(id);
    return static_cast<std::size_t>((bits & kOpaquePipelineDoubleSidedBit) != 0 ? 1u : 0u)
         | static_cast<std::size_t>((bits & kOpaquePipelineMaskedBit) != 0 ? 2u : 0u);
}

struct MeshPushConstants
{
    Vec4 BaseColor;
    Vec4 EmissiveFactor;
    float NormalScale = 1.0f;
    float RoughnessFactor = 1.0f;
    float MetallicFactor = 0.0f;
    float SpecularIntensity = 0.5f;
    std::uint32_t BaseColorTextureIndex = UINT32_MAX;
    std::uint32_t NormalTextureIndex = UINT32_MAX;
    std::uint32_t OrmTextureIndex = UINT32_MAX;
    std::uint32_t EmissiveTextureIndex = UINT32_MAX;
    std::uint32_t ReceiveShadows = 1;
    // Bindless slot of the zone's baked-lighting atlas; UINT32_MAX skips the
    // baked term. Uniform per run (part of the run-merge identity).
    std::uint32_t LightmapTextureIndex = UINT32_MAX;
    // Bindless slot of the zone's baked-AO plane (lightmap UVs); UINT32_MAX
    // leaves ambient unmodulated. Uniform per run, merge identity too.
    std::uint32_t AoTextureIndex = UINT32_MAX;
    // Fragments whose base-colour alpha falls below this are discarded, in the
    // masked pipeline variants only. Ignored by the others, so an opaque
    // material keeps its early-depth path.
    float AlphaCutoff = 0.0f;
};

// Binding 1 of the mesh vertex input: one entry per drawn instance, written
// into per-frame scratch by BindInstanceStream.
struct MeshInstanceData
{
    Mat4 World;
    // Remaps the mesh's lightmap UVs into its atlas rect (uv * xy + zw);
    // identity for cooked cells, whose UVs are absolute atlas coordinates.
    Vec4 LightmapScaleBias;
};

class MeshForwardPass
{
public:
    // `bindings` backs set 2 of the pipeline layout; it must be set up (at
    // least dummy-backed) before this call, or the pass stays inert and
    // draws nothing.
    void Setup(const RendererServices& services, LightBindings& bindings);
    // `skinnedMeshes` resolves items whose SkinnedMesh handle is valid (rest
    // geometry today); null makes those items skip, and a host with no skinned
    // content passes nothing.
    void Draw(const FrameContext& frame,
              const CameraRenderData& camera,
              const RenderLightSet& lights,
              const RenderQueue& queue,
              StaticMeshCache& meshes,
              MaterialCache& materials,
              const SkinnedMeshCache* skinnedMeshes = nullptr,
              Vec4 tint = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
    void Teardown();

    // Pass-local totals, maintained unconditionally at run granularity (the
    // 9.2 cost policy) and copied into RenderStats by the owning feature
    // when counters are active. Also a consumed test seam.
    struct DrawStats
    {
        uint32_t QueueItems = 0;
        uint32_t DrawCalls = 0;
        uint32_t Triangles = 0;
        uint32_t PipelineSwitches = 0;
        // Push-constant uploads. Equal to DrawCalls until the pass skips
        // redundant material state; the counter exists to show exactly that.
        uint32_t MaterialSwitches = 0;
        // Set when the pass had queue items and recorded no draws at all:
        // missing pipelines, or a frame-scratch request it could not serve.
        // The instances that went unrendered as a result are counted too, so
        // a frame that dropped its scene cannot read as a cheap frame.
        bool Skipped = false;
        uint32_t InstancesDropped = 0;
    };
    [[nodiscard]] DrawStats GetLastDrawStats() const { return LastStats; }

private:
    [[nodiscard]] bool EnsurePipelines(const FrameContext& frame);
    [[nodiscard]] bool EnsureTransparentPipelines(const FrameContext& frame);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    [[nodiscard]] bool EnsureDebugPipelines(const FrameContext& frame,
                                            bool overdraw);
#endif
    [[nodiscard]] std::optional<VkDeviceSize> UploadFrameUniforms(
        const CameraRenderData& camera, const RenderLightSet& lights);
    // Uploads and binds the instance stream -- the opaque draw order followed
    // by the view's transparent order -- returning how many entries it covers.
    // Zero means the slice had no room at all. A short grant clips from the
    // tail, so transparent items drop before world geometry does.
    [[nodiscard]] uint32_t BindInstanceStream(const FrameContext& frame,
                                             const RenderQueue& queue);
    void BindFrameState(const FrameContext& frame, VkDeviceSize uniformOffset);
    // Draws are clipped to `streamedInstances`: a run past the stream has no
    // instance data to read.
    void DrawRuns(const FrameContext& frame, const RenderQueue& queue,
                  StaticMeshCache& meshes, MaterialCache& materials,
                  const SkinnedMeshCache* skinnedMeshes, Vec4 tint,
                  uint32_t streamedInstances);
    // One draw per item, in TransparentOrder, never merged: instances inside
    // one draw have no order against each other, and order is the contract.
    void DrawTransparent(const FrameContext& frame, const RenderQueue& queue,
                         StaticMeshCache& meshes, MaterialCache& materials,
                         const SkinnedMeshCache* skinnedMeshes, Vec4 tint,
                         uint32_t streamedInstances);

    VulkanBufferService* Buffers = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    LightBindings* Bindings = nullptr;
    VkDevice Device = VK_NULL_HANDLE;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    ShaderHandle DebugFragmentShader;
#endif
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    // One variant per OpaquePipelineId: lit and unlit, back-face culled and
    // double-sided, opaque and alpha-masked.
    PipelineVariantSet<kOpaquePipelineCount, AttachmentFormatKey> OpaquePipelines;
    // Blended geometry: depth test on, depth write off, straight-alpha over.
    PipelineVariantSet<kTransparentPipelineCount, AttachmentFormatKey> TransparentPipelines;
    // Per-view draw order for the transparent list, retained across frames so
    // the per-view sort does not reallocate.
    std::vector<std::uint32_t> TransparentOrder;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    // The debug families carry the cull and alpha-mask axes and drop the
    // lit/unlit one -- the channel itself is a frame-uniform value, not a
    // pipeline variant. A queue item's pipeline id is folded down to those two
    // bits by DebugPipelineIndex before indexing them. Masking has to survive
    // into the debug views or they describe geometry the lit pass cut away.
    PipelineVariantSet<kDebugPipelineCount, AttachmentFormatKey> DebugPipelines;
    PipelineVariantSet<kDebugPipelineCount, AttachmentFormatKey> OverdrawPipelines;
    RenderDebugView ActiveDebugView = RenderDebugView::None;
#endif
    MeshDrawSubmitter Submitter;
    DrawStats LastStats;
};
