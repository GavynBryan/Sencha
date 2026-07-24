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
static_assert(sizeof(MeshPushConstants) == 80);

static_assert(offsetof(MeshInstanceData, World) == 0);
static_assert(offsetof(MeshInstanceData, LightmapScaleBias) == 64);
static_assert(sizeof(MeshInstanceData) == 80);

static_assert(offsetof(MeshFrameUniforms, ViewProjection) == 0);
static_assert(offsetof(MeshFrameUniforms, ViewPositionTime) == 64);
static_assert(offsetof(MeshFrameUniforms, AmbientSky) == 80);
static_assert(offsetof(MeshFrameUniforms, AmbientGround) == 96);
static_assert(offsetof(MeshFrameUniforms, StyleParams) == 112);
static_assert(offsetof(MeshFrameUniforms, LightCount) == 128);
static_assert(offsetof(MeshFrameUniforms, TonemapEnabled) == 132);
static_assert(offsetof(MeshFrameUniforms, ShadowDarkness) == 136);
static_assert(offsetof(MeshFrameUniforms, Lights) == 144);
static_assert(offsetof(MeshFrameUniforms, SpotShadowCount) == 4240);
static_assert(offsetof(MeshFrameUniforms, SpotShadows) == 4256);
static_assert(offsetof(MeshFrameUniforms, PointShadowCount) == 5024);
static_assert(offsetof(MeshFrameUniforms, PointShadows) == 5040);
static_assert(offsetof(MeshFrameUniforms, ProbeVolumeCount) == 5168);
static_assert(offsetof(MeshFrameUniforms, ProbeVolumes) == 5184);
static_assert(offsetof(MeshFrameUniforms, DebugView) == 5696);
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

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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

    Descriptors->SetFrameUniformBuffer(Scratch->GetBuffer(), sizeof(MeshFrameUniforms));

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
    bool complete = true;
    for (VkPipeline pipeline : OpaquePipelines)
        complete = complete && pipeline != VK_NULL_HANDLE;
    if (complete
        && CachedColorFormat == frame.TargetFormat
        && CachedDepthFormat == frame.DepthFormat)
    {
        return true;
    }

    GraphicsPipelineDesc base{};
    base.VertexShader = VertexShader;
    base.FragmentShader = FragmentShader;
    base.Layout = PipelineLayout;
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
    base.DepthTest = true;
    base.DepthWrite = true;
    base.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    base.ColorBlend = { ColorBlendAttachmentDesc{} };
    base.ColorFormats = { frame.TargetFormat };
    base.DepthFormat = frame.DepthFormat;

    static constexpr const char* kPipelineNames[4] = {
        "Forward/StandardLitBack",
        "Forward/StandardLitDoubleSided",
        "Forward/UnlitBack",
        "Forward/UnlitDoubleSided",
    };
    for (uint32_t index = 0; index < OpaquePipelines.size(); ++index)
    {
        GraphicsPipelineDesc desc = base;
        const bool unlit = index >= static_cast<uint32_t>(OpaquePipelineId::UnlitBack);
        const bool doubleSided = (index & 1u) != 0;
        desc.FragmentSpecializationConstants = {
            ShaderSpecializationConstant{ .Id = 0, .Value = unlit ? 1u : 0u }
        };
        desc.CullMode = doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        OpaquePipelines[index] = Pipelines->GetGraphicsPipeline(desc);
        if (OpaquePipelines[index] == VK_NULL_HANDLE)
            return false;
        VulkanDebugLabels::NameObject(
            Device, VK_OBJECT_TYPE_PIPELINE,
            reinterpret_cast<std::uint64_t>(OpaquePipelines[index]),
            kPipelineNames[index]);
    }

    CachedColorFormat = frame.TargetFormat;
    CachedDepthFormat = frame.DepthFormat;
    return true;
}

#ifdef SENCHA_ENABLE_RENDER_PROFILING
bool MeshForwardPass::EnsureDebugPipelines(const FrameContext& frame,
                                           bool overdraw)
{
    std::array<VkPipeline, 2>& pipelines = overdraw
        ? OverdrawPipelines
        : DebugPipelines;
    VkFormat& cachedColor = overdraw
        ? CachedOverdrawColorFormat
        : CachedDebugColorFormat;
    VkFormat& cachedDepth = overdraw
        ? CachedOverdrawDepthFormat
        : CachedDebugDepthFormat;

    bool complete = true;
    for (VkPipeline pipeline : pipelines)
        complete = complete && pipeline != VK_NULL_HANDLE;
    if (complete
        && cachedColor == frame.TargetFormat
        && cachedDepth == frame.DepthFormat)
    {
        return true;
    }

    GraphicsPipelineDesc base{};
    base.VertexShader = VertexShader;
    base.FragmentShader = DebugFragmentShader;
    base.Layout = PipelineLayout;
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
    base.DepthTest = !overdraw;
    base.DepthWrite = !overdraw;
    base.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    ColorBlendAttachmentDesc blend{};
    if (overdraw)
    {
        blend.BlendEnable = true;
        blend.SrcColor = VK_BLEND_FACTOR_ONE;
        blend.DstColor = VK_BLEND_FACTOR_ONE;
        blend.SrcAlpha = VK_BLEND_FACTOR_ZERO;
        blend.DstAlpha = VK_BLEND_FACTOR_ONE;
    }
    base.ColorBlend = { blend };
    base.ColorFormats = { frame.TargetFormat };
    base.DepthFormat = frame.DepthFormat;

    static constexpr const char* kDebugNames[2] = {
        "Forward/DebugBack",
        "Forward/DebugDoubleSided",
    };
    static constexpr const char* kOverdrawNames[2] = {
        "Forward/OverdrawBack",
        "Forward/OverdrawDoubleSided",
    };
    for (std::uint32_t index = 0; index < pipelines.size(); ++index)
    {
        GraphicsPipelineDesc desc = base;
        desc.CullMode = index == 0 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        pipelines[index] = Pipelines->GetGraphicsPipeline(desc);
        if (pipelines[index] == VK_NULL_HANDLE)
            return false;
        VulkanDebugLabels::NameObject(
            Device, VK_OBJECT_TYPE_PIPELINE,
            reinterpret_cast<std::uint64_t>(pipelines[index]),
            overdraw ? kOverdrawNames[index] : kDebugNames[index]);
    }

    cachedColor = frame.TargetFormat;
    cachedDepth = frame.DepthFormat;
    return true;
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

    // A short grant is a prefix of the draw order, not a gap in it: draws
    // index instances by queue position, and every run left over would need
    // slice space that the short grant just proved is gone.
    auto stream = Scratch->AllocateVertexElements(
        static_cast<uint32_t>(order.size()), sizeof(MeshInstanceData));
    if (!stream.IsValid())
        return 0;

    MeshInstanceData* instances = static_cast<MeshInstanceData*>(stream.Grant.Mapped);
    for (uint32_t i = 0; i < stream.Count; ++i)
    {
        const RenderQueueItem& item = items[order[i]];
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
    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkBuffer lastVertexBuffer = VK_NULL_HANDLE;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

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

        uint32_t pipelineIndex = static_cast<uint32_t>(item.Pipeline);
        const VkPipeline* pipelineSet = OpaquePipelines.data();
        std::size_t pipelineCount = OpaquePipelines.size();
#ifdef SENCHA_ENABLE_RENDER_PROFILING
        if (ActiveDebugView != RenderDebugView::None)
        {
            pipelineIndex &= 1u;
            if (ActiveDebugView == RenderDebugView::Overdraw)
            {
                pipelineSet = OverdrawPipelines.data();
                pipelineCount = OverdrawPipelines.size();
            }
            else
            {
                pipelineSet = DebugPipelines.data();
                pipelineCount = DebugPipelines.size();
            }
        }
#endif
        if (pipelineIndex >= pipelineCount)
            continue;
        const VkPipeline pipeline = pipelineSet[pipelineIndex];
        if (pipeline != lastPipeline)
        {
            vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            lastPipeline = pipeline;
            ++LastStats.PipelineSwitches;
        }

        const StaticMeshSection& section = mesh->Sections[item.SectionIndex];
        const VkBuffer vertexBuffer = Buffers->GetBuffer(mesh->VertexBuffer);
        const VkBuffer indexBuffer = Buffers->GetBuffer(mesh->IndexBuffer);

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

        if (vertexBuffer != lastVertexBuffer)
        {
            VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
            lastVertexBuffer = vertexBuffer;
        }
        if (indexBuffer != lastIndexBuffer)
        {
            vkCmdBindIndexBuffer(frame.Cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            lastIndexBuffer = indexBuffer;
        }
        vkCmdPushConstants(frame.Cmd, PipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        ++LastStats.MaterialSwitches;
        vkCmdDrawIndexed(frame.Cmd, section.IndexCount, drawCount,
                         section.IndexOffset, 0, run.First);
        ++LastStats.DrawCalls;
        LastStats.Triangles += section.IndexCount / 3u * drawCount;
    }
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
        .QueueItems = static_cast<uint32_t>(queue.OpaqueOrder().size()),
    };

    if (PipelineLayout == VK_NULL_HANDLE || frame.DepthFormat == VK_FORMAT_UNDEFINED)
        return;
    if (queue.OpaqueOrder().empty())
        return;

    // Past this point the pass has work, so every early return is a frame
    // that renders none of it.
    const auto giveUp = [this]
    {
        LastStats.Skipped = true;
        LastStats.InstancesDropped = LastStats.QueueItems;
    };
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    ActiveDebugView = lights.DebugView;
    if (ActiveDebugView == RenderDebugView::None)
    {
        if (!EnsurePipelines(frame))
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
    OpaquePipelines.fill(VK_NULL_HANDLE);
#ifdef SENCHA_ENABLE_RENDER_PROFILING
    DebugPipelines.fill(VK_NULL_HANDLE);
    OverdrawPipelines.fill(VK_NULL_HANDLE);
    ActiveDebugView = RenderDebugView::None;
    CachedDebugColorFormat = VK_FORMAT_UNDEFINED;
    CachedDebugDepthFormat = VK_FORMAT_UNDEFINED;
    CachedOverdrawColorFormat = VK_FORMAT_UNDEFINED;
    CachedOverdrawDepthFormat = VK_FORMAT_UNDEFINED;
#endif
    if (PipelineLayout != VK_NULL_HANDLE && Device != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
    PipelineLayout = VK_NULL_HANDLE;
    CachedColorFormat = VK_FORMAT_UNDEFINED;
    CachedDepthFormat = VK_FORMAT_UNDEFINED;
    Bindings = nullptr;
    Device = VK_NULL_HANDLE;
}
