#include <render/MeshForwardPass.h>

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kMeshForwardFragSpv.h>
#include <shaders/kMeshForwardVertSpv.h>

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
static_assert(sizeof(MeshPushConstants) == 64);

static_assert(offsetof(MeshFrameUniforms, ViewProjection) == 0);
static_assert(offsetof(MeshFrameUniforms, ViewPositionTime) == 64);
static_assert(offsetof(MeshFrameUniforms, AmbientSky) == 80);
static_assert(offsetof(MeshFrameUniforms, AmbientGround) == 96);
static_assert(offsetof(MeshFrameUniforms, StyleParams) == 112);
static_assert(offsetof(MeshFrameUniforms, LightCount) == 128);
static_assert(offsetof(MeshFrameUniforms, TonemapEnabled) == 132);
static_assert(offsetof(MeshFrameUniforms, Lights) == 144);
static_assert(offsetof(GpuLight, PositionRange) == 0);
static_assert(offsetof(GpuLight, DirectionCone) == 16);
static_assert(offsetof(GpuLight, ColorIntensity) == 32);
static_assert(offsetof(GpuLight, Type) == 48);
static_assert(offsetof(GpuLight, ShadowIndex) == 52);
static_assert(offsetof(GpuLight, ConeScale) == 56);
static_assert(offsetof(GpuLight, ConeOffset) == 60);

void MeshForwardPass::Setup(const RendererServices& services)
{
    Buffers = services.Buffers;
    Descriptors = services.Descriptors;
    Scratch = services.Scratch;
    Pipelines = services.Pipelines;
    Shaders = services.Shaders;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kMeshForwardVertSpv, kMeshForwardVertSpvWordCount, "Mesh forward vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kMeshForwardFragSpv, kMeshForwardFragSpvWordCount, "Mesh forward fragment");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(MeshPushConstants);
    PipelineLayout = Descriptors->GetPipelineLayout({ push });
    Descriptors->SetFrameUniformBuffer(Scratch->GetBuffer(), sizeof(MeshFrameUniforms));
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
        { 1, sizeof(Mat4), VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    base.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Normal) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(StaticMeshVertex, Uv0) },
        { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
        { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 },
        { 7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(StaticMeshVertex, Tangent) },
    };
    base.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    base.DepthTest = true;
    base.DepthWrite = true;
    base.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    base.ColorBlend = { ColorBlendAttachmentDesc{} };
    base.ColorFormats = { frame.TargetFormat };
    base.DepthFormat = frame.DepthFormat;

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
    }

    CachedColorFormat = frame.TargetFormat;
    CachedDepthFormat = frame.DepthFormat;
    return true;
}

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

    const std::uint32_t lightCount =
        lights.Count < kMaxForwardLights ? lights.Count : kMaxForwardLights;
    uniforms.LightCount = lightCount;
    std::memcpy(uniforms.Lights, lights.Lights, sizeof(GpuLight) * lightCount);

    auto allocation = Scratch->AllocateUniform(sizeof(MeshFrameUniforms));
    if (!allocation.IsValid())
        return std::nullopt;
    std::memcpy(allocation.Mapped, &uniforms, sizeof(uniforms));
    return allocation.Offset;
}

bool MeshForwardPass::BindInstanceStream(const FrameContext& frame, const RenderQueue& queue)
{
    const std::vector<RenderQueueItem>& items = queue.Opaque();
    const std::vector<uint32_t>& order = queue.OpaqueOrder();

    auto stream = Scratch->AllocateVertex(order.size() * sizeof(Mat4));
    if (!stream.IsValid())
        return false;

    Mat4* transforms = static_cast<Mat4*>(stream.Mapped);
    for (size_t i = 0; i < order.size(); ++i)
        transforms[i] = items[order[i]].WorldMatrix.Transposed();

    VkBuffer instanceBuffer = Buffers->GetBuffer(stream.Buffer);
    vkCmdBindVertexBuffers(frame.Cmd, 1, 1, &instanceBuffer, &stream.Offset);
    return true;
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
}

void MeshForwardPass::DrawRuns(const FrameContext& frame, const RenderQueue& queue,
                               StaticMeshCache& meshes, MaterialCache& materials, Vec4 tint)
{
    const std::vector<RenderQueueItem>& items = queue.Opaque();
    const std::vector<uint32_t>& order = queue.OpaqueOrder();
    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkBuffer lastVertexBuffer = VK_NULL_HANDLE;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    for (const RenderQueueRun& run : queue.OpaqueRuns())
    {
        const RenderQueueItem& item = items[order[run.First]];
        const GpuStaticMesh* mesh = meshes.Get(item.Mesh);
        const Material* material = materials.Get(item.Material);
        if (mesh == nullptr || material == nullptr || item.SectionIndex >= mesh->Sections.size())
            continue;

        const uint32_t pipelineIndex = static_cast<uint32_t>(item.Pipeline);
        if (pipelineIndex >= OpaquePipelines.size())
            continue;
        const VkPipeline pipeline = OpaquePipelines[pipelineIndex];
        if (pipeline != lastPipeline)
        {
            vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            lastPipeline = pipeline;
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
        vkCmdDrawIndexed(frame.Cmd, section.IndexCount, run.Count,
                         section.IndexOffset, 0, run.First);
        ++LastStats.DrawCalls;
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
        .DrawCalls = 0,
    };

    if (PipelineLayout == VK_NULL_HANDLE || frame.DepthFormat == VK_FORMAT_UNDEFINED)
        return;
    if (queue.OpaqueOrder().empty())
        return;
    if (!EnsurePipelines(frame))
        return;

    const std::optional<VkDeviceSize> uniformOffset = UploadFrameUniforms(camera, lights);
    if (!uniformOffset.has_value())
        return;
    if (!BindInstanceStream(frame, queue))
        return;

    BindFrameState(frame, *uniformOffset);
    DrawRuns(frame, queue, meshes, materials, tint);
}

void MeshForwardPass::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        Shaders->Destroy(FragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    OpaquePipelines.fill(VK_NULL_HANDLE);
    PipelineLayout = VK_NULL_HANDLE;
    CachedColorFormat = VK_FORMAT_UNDEFINED;
    CachedDepthFormat = VK_FORMAT_UNDEFINED;
}
