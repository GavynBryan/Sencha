#pragma once

#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/Camera.h>
#include <render/LightBindings.h>
#include <render/MaterialCache.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
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
    std::uint32_t Pad2 = 0;
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
    void Draw(const FrameContext& frame,
              const CameraRenderData& camera,
              const RenderLightSet& lights,
              const RenderQueue& queue,
              StaticMeshCache& meshes,
              MaterialCache& materials,
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
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    [[nodiscard]] bool EnsureDebugPipelines(const FrameContext& frame,
                                            bool overdraw);
#endif
    [[nodiscard]] std::optional<VkDeviceSize> UploadFrameUniforms(
        const CameraRenderData& camera, const RenderLightSet& lights);
    // Uploads and binds the instance stream, returning how many draw-order
    // entries it covers. Zero means the slice had no room at all.
    [[nodiscard]] uint32_t BindInstanceStream(const FrameContext& frame,
                                             const RenderQueue& queue);
    void BindFrameState(const FrameContext& frame, VkDeviceSize uniformOffset);
    // Draws are clipped to `streamedInstances`: a run past the stream has no
    // instance data to read.
    void DrawRuns(const FrameContext& frame, const RenderQueue& queue,
                  StaticMeshCache& meshes, MaterialCache& materials, Vec4 tint,
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
    // double-sided.
    PipelineVariantSet<4, AttachmentFormatKey> OpaquePipelines;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    // The debug families carry only the cull axis -- the channel itself is a
    // frame-uniform value, not a pipeline variant -- so a queue item's
    // pipeline id is masked down to its low bit before indexing them.
    PipelineVariantSet<2, AttachmentFormatKey> DebugPipelines;
    PipelineVariantSet<2, AttachmentFormatKey> OverdrawPipelines;
    RenderDebugView ActiveDebugView = RenderDebugView::None;
#endif
    DrawStats LastStats;
};
