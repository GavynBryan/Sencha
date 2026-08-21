#include <render/MeshForwardPass.h>

#include <graphics/vulkan/VulkanDebugLabels.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <shaders/kMeshForwardFragSpv.h>
#include <shaders/kMeshForwardVertSpv.h>
#ifdef SENCHA_ENABLE_RENDER_PROFILING
#include <shaders/kMeshDebugViewFragSpv.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace
{
    // The vertex interface every forward mesh pipeline shares. The lit, unlit,
    // debug-view, and overdraw pipelines differ only in shading and blend
    // state, so the binding and attribute layout is described once: adding a
    // vertex attribute is one edit, not one edit per pipeline family.
    GraphicsPipelineDesc MakeMeshPipelineBase(ShaderHandle vertexShader,
                                              VkPipelineLayout layout)
    {
        GraphicsPipelineDesc base{};
        base.VertexShader = vertexShader;
        base.Layout = layout;
        base.VertexBindings = {
            { 0, sizeof(StaticMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX },
            { 1, sizeof(MeshInstanceData), VK_VERTEX_INPUT_RATE_INSTANCE },
        };
        base.VertexAttributes = {
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Position) },
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Normal) },
            { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(StaticMeshVertex, Uv0) },
            { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshInstanceData, World) },
            { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshInstanceData, World) + 16 },
            { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshInstanceData, World) + 32 },
            { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshInstanceData, World) + 48 },
            { 7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(StaticMeshVertex, Tangent) },
            { 8, 0, VK_FORMAT_R16G16_UNORM, offsetof(StaticMeshVertex, LightmapU) },
            { 9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshInstanceData, LightmapScaleBias) },
        };
        base.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        base.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
        return base;
    }
} // namespace

static_assert(offsetof(MeshPushConstants, BaseColor) == 0);
static_assert(offsetof(MeshPushConstants, EmissiveFactor) == 16);
static_assert(offsetof(MeshPushConstants, NormalScale) == 32);
static_assert(offsetof(MeshPushConstants, RoughnessFactor) == 36);
static_assert(offsetof(MeshPushConstants, MetallicFactor) == 40);
static_assert(offsetof(MeshPushConstants, SpecularIntensity) == 44);
static_assert(offsetof(MeshPushConstants, BaseColorTextureIndex) == 48);
static_assert(offsetof(MeshPushConstants, NormalTextureIndex) == 52);
static_assert(offsetof(MeshPushConstants, OrmTextureIndex) == 56);
static_assert(offsetof(MeshPushConstants, EmissiveTextureIndex) == 60);
static_assert(offsetof(MeshPushConstants, ReceiveShadows) == 64);
static_assert(offsetof(MeshPushConstants, LightmapTextureIndex) == 68);
static_assert(offsetof(MeshPushConstants, AoTextureIndex) == 72);
static_assert(offsetof(MeshPushConstants, AlphaCutoff) == 76);
static_assert(sizeof(MeshPushConstants) == 80);

static_assert(offsetof(MeshInstanceData, World) == 0);
static_assert(offsetof(MeshInstanceData, LightmapScaleBias) == 64);
static_assert(sizeof(MeshInstanceData) == 80);

// The frame block's layout contract, in chained form: each field starts where
// the previous one ends, which is the std140 property the GLSL mirror relies on
// -- the compiler inserted no padding of its own, only the explicit pad
// members. Chained rather than absolute so that inserting a field (a cascade
// array is the expected one) renumbers nothing below it; the absolute anchors
// are the first field and the total size at the end, and only the size is a
// conscious edit on insertion.
static_assert(offsetof(MeshFrameUniforms, ViewProjection) == 0);
static_assert(offsetof(MeshFrameUniforms, ViewPositionTime)
              == offsetof(MeshFrameUniforms, ViewProjection) + sizeof(Mat4));
static_assert(offsetof(MeshFrameUniforms, AmbientSky)
              == offsetof(MeshFrameUniforms, ViewPositionTime) + sizeof(Vec4));
static_assert(offsetof(MeshFrameUniforms, AmbientGround)
              == offsetof(MeshFrameUniforms, AmbientSky) + sizeof(Vec4));
static_assert(offsetof(MeshFrameUniforms, StyleParams)
              == offsetof(MeshFrameUniforms, AmbientGround) + sizeof(Vec4));
static_assert(offsetof(MeshFrameUniforms, LightCount)
              == offsetof(MeshFrameUniforms, StyleParams) + sizeof(Vec4));
// Four scalars -- LightCount, TonemapEnabled, ShadowDarkness, BakedDirectEnabled
// -- fill one 16-byte slot, so the light array lands aligned with no hidden pad.
static_assert(offsetof(MeshFrameUniforms, Lights)
              == offsetof(MeshFrameUniforms, LightCount) + 4 * sizeof(std::uint32_t));
static_assert(offsetof(MeshFrameUniforms, SpotShadowCount)
              == offsetof(MeshFrameUniforms, Lights) + sizeof(GpuLight) * kMaxForwardLights);
// Count plus three more scalars (BakedAoEnabled and two pads) per section, so
// each array starts on the 16-byte boundary std140 gives its GLSL twin.
static_assert(offsetof(MeshFrameUniforms, SpotShadows)
              == offsetof(MeshFrameUniforms, SpotShadowCount) + 4 * sizeof(std::uint32_t));
static_assert(offsetof(MeshFrameUniforms, PointShadowCount)
              == offsetof(MeshFrameUniforms, SpotShadows)
                     + sizeof(GpuSpotShadow) * kMaxSpotShadows);
static_assert(offsetof(MeshFrameUniforms, PointShadows)
              == offsetof(MeshFrameUniforms, PointShadowCount) + 4 * sizeof(std::uint32_t));
static_assert(offsetof(MeshFrameUniforms, ProbeVolumeCount)
              == offsetof(MeshFrameUniforms, PointShadows)
                     + sizeof(GpuPointShadow) * kMaxPointShadows);
static_assert(offsetof(MeshFrameUniforms, ProbeVolumes)
              == offsetof(MeshFrameUniforms, ProbeVolumeCount) + 4 * sizeof(std::uint32_t));
static_assert(offsetof(MeshFrameUniforms, DebugView)
              == offsetof(MeshFrameUniforms, ProbeVolumes)
                     + sizeof(GpuProbeVolume) * kMaxActiveProbeVolumes);
static_assert(sizeof(GpuSpotShadow) == 96);
static_assert(offsetof(GpuSpotShadow, ViewProjection) == 0);
static_assert(offsetof(GpuSpotShadow, AtlasScaleBias) == 64);
static_assert(offsetof(GpuSpotShadow, SamplingParams) == 80);
static_assert(sizeof(GpuPointShadow) == 32);
static_assert(offsetof(GpuPointShadow, PositionFar) == 0);
static_assert(offsetof(GpuPointShadow, Params) == 16);
static_assert(sizeof(MeshFrameUniforms) == 5712);
static_assert(offsetof(GpuLight, PositionRange) == 0);
static_assert(offsetof(GpuLight, DirectionCone) == 16);
static_assert(offsetof(GpuLight, ColorIntensity) == 32);
static_assert(offsetof(GpuLight, Type) == 48);
static_assert(offsetof(GpuLight, ShadowIndex) == 52);
static_assert(offsetof(GpuLight, ConeScale) == 56);
static_assert(offsetof(GpuLight, ConeOffset) == 60);

void MeshForwardPass::Setup(const RendererServices& services, LightBindings& bindings)
{
    Buffers = services.Buffers;
    Descriptors = services.Descriptors;
    Scratch = services.Scratch;
    Pipelines = services.Pipelines;
    Shaders = services.Shaders;
    Bindings = &bindings;
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kMeshForwardVertSpv, kMeshForwardVertSpvWordCount, "Mesh forward vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kMeshForwardFragSpv, kMeshForwardFragSpvWordCount, "Mesh forward fragment");
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    DebugFragmentShader = Shaders->CreateModuleFromSpirv(
        kMeshDebugViewFragSpv, kMeshDebugViewFragSpvWordCount,
        "Mesh debug-view fragment");
#endif

    // Without valid lighting bindings there is no legal set-2 layout to
    // build against; leaving PipelineLayout null keeps Draw inert.
    if (!bindings.IsValid())
        return;

    // Fragment only. The vertex stage carried a copy of the block for years
    // without reading a field of it, and the copy drifted -- stale names over
    // the right offsets. One mirror, one stage: a vertex-stage consumer must
    // widen this *and* the vkCmdPushConstants flags below together.
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(MeshPushConstants);

    const VkDescriptorSetLayout setLayouts[] = {
        Descriptors->GetFrameSetLayout(),
        Descriptors->GetBindlessSetLayout(),
        bindings.GetSetLayout(),
    };
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 3;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout) != VK_SUCCESS)
        PipelineLayout = VK_NULL_HANDLE;

    Descriptors->RequireFrameUniformRange(Scratch->GetBuffer(), sizeof(MeshFrameUniforms));

    // Build the pipeline set now rather than inside the first Draw. Both
    // formats are already known here, and driver compilation of the four
    // variants costs tens of milliseconds: paid at load it is invisible, paid
    // on the first visible frame it is a hitch. A later format change still
    // falls back to the rebuild inside EnsurePipelines.
    if (PipelineLayout != VK_NULL_HANDLE && services.Swapchain != nullptr)
    {
        FrameContext prewarm{};
        prewarm.TargetFormat = services.Swapchain->GetFormat();
        prewarm.DepthFormat = services.DepthFormat;
        if (prewarm.TargetFormat != VK_FORMAT_UNDEFINED
            && prewarm.DepthFormat != VK_FORMAT_UNDEFINED)
        {
            (void)EnsurePipelines(prewarm);
        }
    }
}

bool MeshForwardPass::EnsurePipelines(const FrameContext& frame)
{
    static constexpr const char* kPipelineNames[kOpaquePipelineCount] = {
        "Forward/StandardLitBack",
        "Forward/StandardLitDoubleSided",
        "Forward/UnlitBack",
        "Forward/UnlitDoubleSided",
        "Forward/StandardLitBackMasked",
        "Forward/StandardLitDoubleSidedMasked",
        "Forward/UnlitBackMasked",
        "Forward/UnlitDoubleSidedMasked",
    };

    return OpaquePipelines.Ensure(
        AttachmentFormatKey{ frame.TargetFormat, frame.DepthFormat },
        [&](std::size_t index) {
            GraphicsPipelineDesc desc = MakeMeshPipelineBase(VertexShader, PipelineLayout);
            desc.FragmentShader = FragmentShader;
            desc.DepthTest = true;
            desc.DepthWrite = true;
            desc.ColorBlend = { ColorBlendAttachmentDesc{} };
            desc.ColorFormats = { frame.TargetFormat };
            desc.DepthFormat = frame.DepthFormat;

            // Read the axes as bits. The old test for unlit was "index is at
            // or past UnlitBack", which stops being true the moment a third
            // axis is added above it.
            const bool doubleSided = (index & kOpaquePipelineDoubleSidedBit) != 0;
            const bool unlit = (index & kOpaquePipelineUnlitBit) != 0;
            const bool masked = (index & kOpaquePipelineMaskedBit) != 0;
            desc.FragmentSpecializationConstants = {
                ShaderSpecializationConstant{ .Id = 0, .Value = unlit ? 1u : 0u },
                ShaderSpecializationConstant{ .Id = 1, .Value = masked ? 1u : 0u }
            };
            desc.CullMode = doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

            const VkPipeline pipeline = Pipelines->GetGraphicsPipeline(desc);
            if (pipeline != VK_NULL_HANDLE)
            {
                VulkanDebugLabels::NameObject(Device, VK_OBJECT_TYPE_PIPELINE,
                                              reinterpret_cast<std::uint64_t>(pipeline),
                                              kPipelineNames[index]);
            }
            return pipeline;
        });
}

bool MeshForwardPass::EnsureTransparentPipelines(const FrameContext& frame)
{
    static constexpr const char* kTransparentNames[kTransparentPipelineCount] = {
        "Forward/TransparentLitBack",
        "Forward/TransparentLitDoubleSided",
        "Forward/TransparentUnlitBack",
        "Forward/TransparentUnlitDoubleSided",
    };

    return TransparentPipelines.Ensure(
        AttachmentFormatKey{ frame.TargetFormat, frame.DepthFormat },
        [&](std::size_t index) {
            GraphicsPipelineDesc desc = MakeMeshPipelineBase(VertexShader, PipelineLayout);
            desc.FragmentShader = FragmentShader;
            // Test against the opaque scene, never behind it; write nothing,
            // so blended surfaces do not occlude each other by draw order.
            desc.DepthTest = true;
            desc.DepthWrite = false;
            // Straight-alpha over, matching the shader's unpremultiplied
            // output. Alpha composites as over too -- ONE, not SRC_ALPHA -- so
            // a target that started opaque stays opaque and the editor's UI
            // composite cannot see through the viewport.
            ColorBlendAttachmentDesc blend{};
            blend.BlendEnable = true;
            blend.SrcColor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.DstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
            blend.DstAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            desc.ColorBlend = { blend };
            desc.ColorFormats = { frame.TargetFormat };
            desc.DepthFormat = frame.DepthFormat;

            const bool doubleSided = (index & kOpaquePipelineDoubleSidedBit) != 0;
            const bool unlit = (index & kOpaquePipelineUnlitBit) != 0;
            desc.FragmentSpecializationConstants = {
                ShaderSpecializationConstant{ .Id = 0, .Value = unlit ? 1u : 0u },
                ShaderSpecializationConstant{ .Id = 1, .Value = 0u }
            };
            desc.CullMode = doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;

            const VkPipeline pipeline = Pipelines->GetGraphicsPipeline(desc);
            if (pipeline != VK_NULL_HANDLE)
            {
                VulkanDebugLabels::NameObject(Device, VK_OBJECT_TYPE_PIPELINE,
                                              reinterpret_cast<std::uint64_t>(pipeline),
                                              kTransparentNames[index]);
            }
            return pipeline;
        });
}

#ifdef SENCHA_ENABLE_RENDER_PROFILING
bool MeshForwardPass::EnsureDebugPipelines(const FrameContext& frame,
                                           bool overdraw)
{
    static constexpr const char* kDebugNames[kDebugPipelineCount] = {
        "Forward/DebugBack",
        "Forward/DebugDoubleSided",
        "Forward/DebugBackMasked",
        "Forward/DebugDoubleSidedMasked",
    };
    static constexpr const char* kOverdrawNames[kDebugPipelineCount] = {
        "Forward/OverdrawBack",
        "Forward/OverdrawDoubleSided",
        "Forward/OverdrawBackMasked",
        "Forward/OverdrawDoubleSidedMasked",
    };

    // Overdraw counts fragments rather than shading them, so it drops the
    // depth test and blends additively; the two families are otherwise the
    // same debug shader over the same cull axis.
    auto build = [&](std::size_t index) {
        GraphicsPipelineDesc desc = MakeMeshPipelineBase(VertexShader, PipelineLayout);
        desc.FragmentShader = DebugFragmentShader;
        desc.DepthTest = !overdraw;
        desc.DepthWrite = !overdraw;
        ColorBlendAttachmentDesc blend{};
        if (overdraw)
        {
            blend.BlendEnable = true;
            blend.SrcColor = VK_BLEND_FACTOR_ONE;
            blend.DstColor = VK_BLEND_FACTOR_ONE;
            blend.SrcAlpha = VK_BLEND_FACTOR_ZERO;
            blend.DstAlpha = VK_BLEND_FACTOR_ONE;
        }
        desc.ColorBlend = { blend };
        desc.ColorFormats = { frame.TargetFormat };
        desc.DepthFormat = frame.DepthFormat;
        desc.CullMode = (index & 1u) != 0 ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        // Only the mask constant: the debug shader has no lit/unlit variant.
        desc.FragmentSpecializationConstants = {
            ShaderSpecializationConstant{ .Id = 1, .Value = (index & 2u) != 0 ? 1u : 0u }
        };

        const VkPipeline pipeline = Pipelines->GetGraphicsPipeline(desc);
        if (pipeline != VK_NULL_HANDLE)
        {
            VulkanDebugLabels::NameObject(Device, VK_OBJECT_TYPE_PIPELINE,
                                          reinterpret_cast<std::uint64_t>(pipeline),
                                          overdraw ? kOverdrawNames[index] : kDebugNames[index]);
        }
        return pipeline;
    };

    const AttachmentFormatKey key{ frame.TargetFormat, frame.DepthFormat };
    return overdraw ? OverdrawPipelines.Ensure(key, build)
                    : DebugPipelines.Ensure(key, build);
}
#endif

std::optional<VkDeviceSize> MeshForwardPass::UploadFrameUniforms(
    const CameraRenderData& camera, const RenderLightSet& lights)
{
    MeshFrameUniforms uniforms{};
    uniforms.ViewProjection = camera.ViewProjection.Transposed();
    uniforms.ViewPositionTime = Vec4(camera.Position.X, camera.Position.Y, camera.Position.Z, 0.0f);
    uniforms.AmbientSky = Vec4(lights.AmbientSky.X, lights.AmbientSky.Y, lights.AmbientSky.Z, 0.0f);
    uniforms.AmbientGround = Vec4(lights.AmbientGround.X, lights.AmbientGround.Y, lights.AmbientGround.Z, 0.0f);
    uniforms.StyleParams = Vec4(lights.DiffuseWrap, lights.MinAmbient,
                                lights.Exposure, lights.TonemapKnee);
    uniforms.TonemapEnabled = lights.TonemapEnabled ? 1u : 0u;
    uniforms.ShadowDarkness = lights.ShadowDarkness;
    uniforms.BakedDirectEnabled = lights.BakedDirectEnabled ? 1u : 0u;
    uniforms.BakedAoEnabled = lights.BakedAoEnabled ? 1u : 0u;

    const std::uint32_t lightCount =
        lights.Count < kMaxForwardLights ? lights.Count : kMaxForwardLights;
    uniforms.LightCount = lightCount;
    std::memcpy(uniforms.Lights, lights.Lights, sizeof(GpuLight) * lightCount);

    const std::uint32_t shadowCount =
        lights.SpotShadowCount < kMaxSpotShadows
            ? lights.SpotShadowCount
            : kMaxSpotShadows;
    uniforms.SpotShadowCount = shadowCount;
    for (std::uint32_t index = 0; index < shadowCount; ++index)
    {
        uniforms.SpotShadows[index].ViewProjection =
            lights.SpotShadows[index].ViewProjection.Transposed();
        uniforms.SpotShadows[index].AtlasScaleBias =
            lights.SpotShadows[index].AtlasScaleBias;
        uniforms.SpotShadows[index].SamplingParams =
            lights.SpotShadows[index].SamplingParams;
    }

    const std::uint32_t pointShadowCount =
        lights.PointShadowCount < kMaxPointShadows
            ? lights.PointShadowCount
            : kMaxPointShadows;
    uniforms.PointShadowCount = pointShadowCount;
    for (std::uint32_t index = 0; index < pointShadowCount; ++index)
    {
        uniforms.PointShadows[index].PositionFar =
            lights.PointShadows[index].PositionFar;
        uniforms.PointShadows[index].Params =
            lights.PointShadows[index].Params;
    }

    const std::uint32_t probeVolumeCount =
        lights.ProbeVolumeCount < kMaxActiveProbeVolumes
            ? lights.ProbeVolumeCount
            : kMaxActiveProbeVolumes;
    uniforms.ProbeVolumeCount = probeVolumeCount;
    std::memcpy(uniforms.ProbeVolumes, lights.ProbeVolumes,
                sizeof(GpuProbeVolume) * probeVolumeCount);

#ifdef SENCHA_ENABLE_RENDER_PROFILING
    uniforms.DebugView = static_cast<std::uint32_t>(lights.DebugView);
#endif

    auto allocation = Scratch->AllocateUniform(sizeof(MeshFrameUniforms));
    if (!allocation.IsValid())
        return std::nullopt;
    std::memcpy(allocation.Mapped, &uniforms, sizeof(uniforms));
    return allocation.Offset;
}

uint32_t MeshForwardPass::BindInstanceStream(const FrameContext& frame,
                                             const RenderQueue& queue)
{
    const std::vector<RenderQueueItem>& items = queue.Opaque();
    const std::vector<uint32_t>& order = queue.OpaqueOrder();
    const std::vector<RenderQueueItem>& transparent = queue.Transparent();

    // A short grant is a prefix of the draw order, not a gap in it: draws
    // index instances by queue position, and every run left over would need
    // slice space that the short grant just proved is gone. Transparent
    // instances sit at the tail, after every opaque one, so they are also the
    // first to be clipped -- effects drop before world geometry does.
    auto stream = Scratch->AllocateVertexElements(
        static_cast<uint32_t>(order.size() + TransparentOrder.size()),
        sizeof(MeshInstanceData));
    if (!stream.IsValid())
        return 0;

    const uint32_t opaqueCount =
        std::min(stream.Count, static_cast<uint32_t>(order.size()));
    MeshInstanceData* instances = static_cast<MeshInstanceData*>(stream.Grant.Mapped);
    for (uint32_t i = 0; i < opaqueCount; ++i)
    {
        const RenderQueueItem& item = items[order[i]];
        instances[i].World = item.WorldMatrix.Transposed();
        instances[i].LightmapScaleBias = item.LightmapScaleBias;
    }
    for (uint32_t i = opaqueCount; i < stream.Count; ++i)
    {
        const RenderQueueItem& item =
            transparent[TransparentOrder[i - opaqueCount]];
        instances[i].World = item.WorldMatrix.Transposed();
        instances[i].LightmapScaleBias = item.LightmapScaleBias;
    }

    VkBuffer instanceBuffer = Buffers->GetBuffer(stream.Grant.Buffer);
    vkCmdBindVertexBuffers(frame.Cmd, 1, 1, &instanceBuffer, &stream.Grant.Offset);
    return stream.Count;
}

void MeshForwardPass::BindFrameState(const FrameContext& frame, VkDeviceSize uniformOffset)
{
    VkViewport viewport{};
    viewport.width = static_cast<float>(frame.TargetExtent.width);
    viewport.height = static_cast<float>(frame.TargetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = frame.TargetExtent;

    vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

    const uint32_t dynamicOffset = static_cast<uint32_t>(uniformOffset);
    const VkDescriptorSet frameSet = Descriptors->GetFrameSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            0, 1, &frameSet, 1, &dynamicOffset);
    const VkDescriptorSet bindlessSet = Descriptors->GetBindlessSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            1, 1, &bindlessSet, 0, nullptr);
    const VkDescriptorSet lightingSet = Bindings->GetSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            2, 1, &lightingSet, 0, nullptr);
}

void MeshForwardPass::DrawRuns(const FrameContext& frame, const RenderQueue& queue,
                               StaticMeshCache& meshes, MaterialCache& materials,
                               Vec4 tint, uint32_t streamedInstances)
{
    const std::vector<RenderQueueItem>& items = queue.Opaque();
    const std::vector<uint32_t>& order = queue.OpaqueOrder();

    // BindFrameState and the instance stream have just been bound, so nothing
    // this submitter recorded on an earlier frame still describes the buffer.
    Submitter.ClearTally();
    Submitter.Invalidate();

    for (const RenderQueueRun& run : queue.OpaqueRuns())
    {
        if (run.First >= streamedInstances)
            continue;
        const uint32_t drawCount =
            std::min(run.Count, streamedInstances - run.First);
        const RenderQueueItem& item = items[order[run.First]];
        const GpuStaticMesh* mesh = meshes.Get(item.Mesh);
        const Material* material = materials.Get(item.Material);
        if (mesh == nullptr || material == nullptr || item.SectionIndex >= mesh->Sections.size())
            continue;

        const auto pipelineIndex = static_cast<std::size_t>(item.Pipeline);
        VkPipeline pipeline = VK_NULL_HANDLE;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        if (ActiveDebugView != RenderDebugView::None)
        {
            // Debug families carry the cull and mask axes only, so the
            // lit/unlit half of the id is dropped rather than indexing past
            // their end.
            const std::size_t debugVariant = DebugPipelineIndex(item.Pipeline);
            pipeline = ActiveDebugView == RenderDebugView::Overdraw
                ? OverdrawPipelines.Get(debugVariant)
                : DebugPipelines.Get(debugVariant);
        }
        else
#endif
        {
            pipeline = OpaquePipelines.Get(pipelineIndex);
        }
        if (pipeline == VK_NULL_HANDLE)
            continue;

        const StaticMeshSection& section = mesh->Sections[item.SectionIndex];

        MeshPushConstants push{};
        push.BaseColor = Vec4{ material->BaseColor.X * tint.X, material->BaseColor.Y * tint.Y,
                               material->BaseColor.Z * tint.Z, material->BaseColor.W * tint.W };
        push.EmissiveFactor = Vec4(material->EmissiveFactor.X,
                                   material->EmissiveFactor.Y,
                                   material->EmissiveFactor.Z,
                                   material->EmissiveStrength);
        push.NormalScale = material->NormalScale;
        push.RoughnessFactor = material->RoughnessFactor;
        push.MetallicFactor = material->MetallicFactor;
        push.SpecularIntensity = material->SpecularIntensity;
        push.BaseColorTextureIndex = material->BaseColorTextureIndex;
        push.NormalTextureIndex = material->NormalTextureIndex;
        push.OrmTextureIndex = material->OrmTextureIndex;
        push.EmissiveTextureIndex = material->EmissiveTextureIndex;
        push.ReceiveShadows = material->ReceiveShadows ? 1u : 0u;
        push.LightmapTextureIndex = item.LightmapTextureIndex;
        push.AoTextureIndex = item.AoTextureIndex;
        // Only the masked variants read it, so an opaque material's value is
        // carried but never consulted.
        push.AlphaCutoff = material->AlphaCutoff;

        // Push constants are layout-scoped rather than pipeline-scoped, so
        // this is independent of whatever the submitter decides to rebind.
        vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        ++LastStats.MaterialSwitches;

        MeshDrawCommand draw{};
        draw.Pipeline = pipeline;
        draw.VertexBuffer = Buffers->GetBuffer(mesh->VertexBuffer);
        draw.IndexBuffer = Buffers->GetBuffer(mesh->IndexBuffer);
        draw.IndexCount = section.IndexCount;
        draw.IndexOffset = section.IndexOffset;
        draw.InstanceCount = drawCount;
        draw.FirstInstance = run.First;
        Submitter.Submit(frame.Cmd, draw);
    }

    const MeshDrawTally& tally = Submitter.Tally();
    LastStats.DrawCalls = tally.Draws;
    LastStats.Triangles = tally.Triangles;
    LastStats.PipelineSwitches = tally.PipelineBinds;
}

void MeshForwardPass::DrawTransparent(const FrameContext& frame, const RenderQueue& queue,
                                      StaticMeshCache& meshes, MaterialCache& materials,
                                      Vec4 tint, uint32_t streamedInstances)
{
    const std::vector<RenderQueueItem>& items = queue.Transparent();
    const uint32_t opaqueCount = static_cast<uint32_t>(queue.OpaqueOrder().size());

    for (uint32_t position = 0; position < TransparentOrder.size(); ++position)
    {
        // Instance data for this draw sits at opaqueCount + position; past the
        // streamed grant there is nothing to read.
        const uint32_t instanceIndex = opaqueCount + position;
        if (instanceIndex >= streamedInstances)
            break;

        const RenderQueueItem& item = items[TransparentOrder[position]];
        const GpuStaticMesh* mesh = meshes.Get(item.Mesh);
        const Material* material = materials.Get(item.Material);
        if (mesh == nullptr || material == nullptr
            || item.SectionIndex >= mesh->Sections.size())
            continue;

        VkPipeline pipeline = VK_NULL_HANDLE;
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        if (ActiveDebugView != RenderDebugView::None)
        {
            // Debug channels inspect geometry, so blended items appear in them
            // -- opaquely, which is the point: a debug view wants the surface,
            // not the composite.
            pipeline = ActiveDebugView == RenderDebugView::Overdraw
                ? OverdrawPipelines.Get(DebugPipelineIndex(item.Pipeline))
                : DebugPipelines.Get(DebugPipelineIndex(item.Pipeline));
        }
        else
#endif
        {
            pipeline = TransparentPipelines.Get(TransparentPipelineIndex(item.Pipeline));
        }
        if (pipeline == VK_NULL_HANDLE)
            continue;

        const StaticMeshSection& section = mesh->Sections[item.SectionIndex];

        MeshPushConstants push{};
        push.BaseColor = Vec4{ material->BaseColor.X * tint.X, material->BaseColor.Y * tint.Y,
                               material->BaseColor.Z * tint.Z, material->BaseColor.W * tint.W };
        push.EmissiveFactor = Vec4(material->EmissiveFactor.X,
                                   material->EmissiveFactor.Y,
                                   material->EmissiveFactor.Z,
                                   material->EmissiveStrength);
        push.NormalScale = material->NormalScale;
        push.RoughnessFactor = material->RoughnessFactor;
        push.MetallicFactor = material->MetallicFactor;
        push.SpecularIntensity = material->SpecularIntensity;
        push.BaseColorTextureIndex = material->BaseColorTextureIndex;
        push.NormalTextureIndex = material->NormalTextureIndex;
        push.OrmTextureIndex = material->OrmTextureIndex;
        push.EmissiveTextureIndex = material->EmissiveTextureIndex;
        push.ReceiveShadows = material->ReceiveShadows ? 1u : 0u;
        push.LightmapTextureIndex = item.LightmapTextureIndex;
        push.AoTextureIndex = item.AoTextureIndex;
        push.AlphaCutoff = material->AlphaCutoff;

        vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        ++LastStats.MaterialSwitches;

        MeshDrawCommand draw{};
        draw.Pipeline = pipeline;
        draw.VertexBuffer = Buffers->GetBuffer(mesh->VertexBuffer);
        draw.IndexBuffer = Buffers->GetBuffer(mesh->IndexBuffer);
        draw.IndexCount = section.IndexCount;
        draw.IndexOffset = section.IndexOffset;
        draw.InstanceCount = 1;
        draw.FirstInstance = instanceIndex;
        Submitter.Submit(frame.Cmd, draw);
    }

    const MeshDrawTally& tally = Submitter.Tally();
    LastStats.DrawCalls = tally.Draws;
    LastStats.Triangles = tally.Triangles;
    LastStats.PipelineSwitches = tally.PipelineBinds;
}

void MeshForwardPass::Draw(const FrameContext& frame,
                           const CameraRenderData& camera,
                           const RenderLightSet& lights,
                           const RenderQueue& queue,
                           StaticMeshCache& meshes,
                           MaterialCache& materials,
                           Vec4 tint)
{
    LastStats = DrawStats{
        .QueueItems = static_cast<uint32_t>(queue.OpaqueOrder().size()
                                            + queue.Transparent().size()),
    };

    if (PipelineLayout == VK_NULL_HANDLE || frame.DepthFormat == VK_FORMAT_UNDEFINED)
        return;
    if (queue.OpaqueOrder().empty() && queue.Transparent().empty())
        return;

    // Back-to-front for this view. Per call rather than per frame: a host may
    // replay one queue under several cameras, and the order belongs to each.
    BuildTransparentOrder(queue.Transparent(), camera.Position, TransparentOrder);

    // Past this point the pass has work, so every early return is a frame
    // that renders none of it.
    const auto giveUp = [this]
    {
        LastStats.Skipped = true;
        LastStats.InstancesDropped = LastStats.QueueItems;
    };
    // The transparent family only has to exist when something blends.
    const bool wantTransparent = !TransparentOrder.empty();
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    ActiveDebugView = lights.DebugView;
    if (ActiveDebugView == RenderDebugView::None)
    {
        if (!EnsurePipelines(frame))
            return giveUp();
        if (wantTransparent && !EnsureTransparentPipelines(frame))
            return giveUp();
    }
    else if (!EnsureDebugPipelines(
                 frame, ActiveDebugView == RenderDebugView::Overdraw))
    {
        return giveUp();
    }
#else
    if (!EnsurePipelines(frame))
        return giveUp();
    if (wantTransparent && !EnsureTransparentPipelines(frame))
        return giveUp();
#endif

    const std::optional<VkDeviceSize> uniformOffset = UploadFrameUniforms(camera, lights);
    if (!uniformOffset.has_value())
        return giveUp();
    const uint32_t streamed = BindInstanceStream(frame, queue);
    if (streamed == 0)
        return giveUp();
    // Whatever the slice could not carry goes unrendered this frame, and is
    // counted rather than silently missing from the image.
    LastStats.InstancesDropped = LastStats.QueueItems - streamed;

    BindFrameState(frame, *uniformOffset);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    if (ActiveDebugView == RenderDebugView::Overdraw)
    {
        VkClearAttachment clear{};
        clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clear.colorAttachment = 0;

        VkClearRect rect{};
        rect.rect.extent = frame.TargetExtent;
        rect.layerCount = 1;
        vkCmdClearAttachments(frame.Cmd, 1, &clear, 1, &rect);
    }
#endif
    DrawRuns(frame, queue, meshes, materials, tint, streamed);
    DrawTransparent(frame, queue, meshes, materials, tint, streamed);
}

void MeshForwardPass::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        Shaders->Destroy(FragmentShader);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        Shaders->Destroy(DebugFragmentShader);
#endif
    }
    VertexShader = {};
    FragmentShader = {};
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    DebugFragmentShader = {};
#endif
    OpaquePipelines.Reset();
    TransparentPipelines.Reset();
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    DebugPipelines.Reset();
    OverdrawPipelines.Reset();
    ActiveDebugView = RenderDebugView::None;
#endif
    if (PipelineLayout != VK_NULL_HANDLE && Device != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
    PipelineLayout = VK_NULL_HANDLE;
    Bindings = nullptr;
    Device = VK_NULL_HANDLE;
}
