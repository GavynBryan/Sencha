#include <render/MeshRenderFeature.h>

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kMeshForwardFragSpv.h>
#include <shaders/kMeshForwardVertSpv.h>

#include <cstddef>
#include <cstring>

MeshRenderFeature::MeshRenderFeature(RenderQueue& queue,
                                     MeshCache& meshes,
                                     MaterialCache& materials,
                                     const CameraRenderData& camera)
    : Queue(&queue)
    , Meshes(&meshes)
    , Materials(&materials)
    , Camera(&camera)
{
}

void MeshRenderFeature::Setup(const RendererServices& services)
{
    Buffers = services.Buffers;
    Descriptors = services.Descriptors;
    Scratch = services.Scratch;
    Pipelines = services.Pipelines;
    Shaders = services.Shaders;
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;

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

void MeshRenderFeature::OnDraw(const FrameContext& frame)
{
    if (Queue == nullptr || PipelineLayout == VK_NULL_HANDLE || frame.DepthFormat == VK_FORMAT_UNDEFINED)
    {
        return;
    }

    if (Pipeline == VK_NULL_HANDLE
        || CachedColorFormat != frame.TargetFormat
        || CachedDepthFormat != frame.DepthFormat)
    {
        GraphicsPipelineDesc desc{};
        desc.VertexShader = VertexShader;
        desc.FragmentShader = FragmentShader;
        desc.Layout = PipelineLayout;
        desc.VertexBindings = {
            { 0, sizeof(StaticMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX },
        };
        desc.VertexAttributes = {
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Position) },
            { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Normal) },
            { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(StaticMeshVertex, Uv0) },
        };
        desc.CullMode = VK_CULL_MODE_BACK_BIT;
        desc.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        desc.DepthTest = true;
        desc.DepthWrite = true;
        desc.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
        desc.ColorBlend = { ColorBlendAttachmentDesc{} };
        desc.ColorFormats = { frame.TargetFormat };
        desc.DepthFormat = frame.DepthFormat;
        Pipeline = Pipelines->GetGraphicsPipeline(desc);
        CachedColorFormat = frame.TargetFormat;
        CachedDepthFormat = frame.DepthFormat;
    }

    if (Pipeline == VK_NULL_HANDLE) return;

    MeshFrameUniforms uniforms{};
    uniforms.ViewProjection = Camera->ViewProjection.Transposed();  // GLSL expects column-major
    uniforms.ViewPositionTime = Vec4(Camera->Position.X, Camera->Position.Y, Camera->Position.Z, 0.0f);

    auto allocation = Scratch->AllocateUniform(sizeof(MeshFrameUniforms));
    if (!allocation.IsValid()) return;
    std::memcpy(allocation.Mapped, &uniforms, sizeof(uniforms));

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(frame.TargetExtent.width);
    viewport.height = static_cast<float>(frame.TargetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = frame.TargetExtent;

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
    vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

    const uint32_t dynamicOffset = static_cast<uint32_t>(allocation.Offset);
    VkDescriptorSet frameSet = Descriptors->GetFrameSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            0, 1, &frameSet, 1, &dynamicOffset);

    for (const RenderQueueItem& item : Queue->Opaque())
    {
        const GpuMesh* mesh = Meshes->Get(item.Mesh);
        const Material* material = Materials->Get(item.Material);
        if (mesh == nullptr || material == nullptr || item.SubmeshIndex >= mesh->Submeshes.size())
        {
            continue;
        }

        const SubmeshRange& submesh = mesh->Submeshes[item.SubmeshIndex];
        VkBuffer vertexBuffer = Buffers->GetBuffer(mesh->VertexBuffer);
        VkBuffer indexBuffer = Buffers->GetBuffer(mesh->IndexBuffer);
        VkDeviceSize vertexOffset = 0;

        MeshPushConstants push{};
        push.World = item.WorldMatrix.Transposed();
        push.BaseColor = material->BaseColor;

        vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
        vkCmdBindIndexBuffer(frame.Cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(frame.Cmd, PipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);
        vkCmdDrawIndexed(frame.Cmd, submesh.IndexCount, 1, submesh.IndexOffset, 0, 0);
    }
}

void MeshRenderFeature::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        Shaders->Destroy(FragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    Pipeline = VK_NULL_HANDLE;
}
