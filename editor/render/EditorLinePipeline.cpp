#include "EditorLinePipeline.h"

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kEditorLineFragSpv.h>
#include <shaders/kEditorLineVertSpv.h>

#include <cstddef>
#include <cstring>

void EditorLinePipeline::Setup(const RendererServices& services)
{
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    Shaders = services.Shaders;
    Pipelines = services.Pipelines;
    Scratch = services.Scratch;
    Buffers = services.Buffers;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kEditorLineVertSpv, kEditorLineVertSpvWordCount, "Editor line vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kEditorLineFragSpv, kEditorLineFragSpvWordCount, "Editor line fragment");

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout);
}

void EditorLinePipeline::Submit(const FrameContext& frame,
                                const EditorViewport& viewport,
                                std::span<const EditorLineVertex> vertices)
{
    if (PipelineLayout == VK_NULL_HANDLE || Scratch == nullptr || Buffers == nullptr
        || frame.DepthFormat == VK_FORMAT_UNDEFINED || vertices.empty())
        return;

    if (Pipeline == VK_NULL_HANDLE || CachedColor != frame.TargetFormat || CachedDepth != frame.DepthFormat)
    {
        GraphicsPipelineDesc desc{};
        desc.VertexShader = VertexShader;
        desc.FragmentShader = FragmentShader;
        desc.Layout = PipelineLayout;
        desc.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        desc.VertexBindings = { { 0, sizeof(EditorLineVertex), VK_VERTEX_INPUT_RATE_VERTEX } };
        desc.VertexAttributes = {
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(EditorLineVertex, Position) },
            { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(EditorLineVertex, Color) },
        };
        desc.CullMode = VK_CULL_MODE_NONE;
        desc.DepthTest = true;
        desc.DepthWrite = false;
        desc.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
        desc.ColorBlend = { ColorBlendAttachmentDesc{} };
        desc.ColorFormats = { frame.TargetFormat };
        desc.DepthFormat = frame.DepthFormat;
        Pipeline = Pipelines->GetGraphicsPipeline(desc);
        CachedColor = frame.TargetFormat;
        CachedDepth = frame.DepthFormat;
    }
    if (Pipeline == VK_NULL_HANDLE)
        return;

    const float vpWidth = viewport.RegionMax.x - viewport.RegionMin.x;
    const float vpHeight = viewport.RegionMax.y - viewport.RegionMin.y;
    if (vpWidth <= 1.0f || vpHeight <= 1.0f)
        return;

    const VkDeviceSize byteCount = sizeof(EditorLineVertex) * vertices.size();
    const auto allocation = Scratch->AllocateVertex(byteCount);
    if (!allocation.IsValid())
        return;

    std::memcpy(allocation.Mapped, vertices.data(), static_cast<size_t>(byteCount));

    VkViewport vkViewport{};
    vkViewport.x = viewport.RegionMin.x;
    vkViewport.y = viewport.RegionMin.y;
    vkViewport.width = vpWidth;
    vkViewport.height = vpHeight;
    vkViewport.minDepth = 0.0f;
    vkViewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {
        static_cast<int32_t>(viewport.RegionMin.x),
        static_cast<int32_t>(viewport.RegionMin.y),
    };
    scissor.extent = { static_cast<uint32_t>(vpWidth), static_cast<uint32_t>(vpHeight) };

    const CameraRenderData renderData = viewport.BuildRenderData();
    const PushConstants push{ .ViewProjection = renderData.ViewProjection.Transposed() };

    VkBuffer vertexBuffer = Buffers->GetBuffer(allocation.Buffer);
    VkDeviceSize vertexOffset = allocation.Offset;

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
    vkCmdSetViewport(frame.Cmd, 0, 1, &vkViewport);
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);
    vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
    vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(frame.Cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
}

void EditorLinePipeline::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        Shaders->Destroy(FragmentShader);
        VertexShader = {};
        FragmentShader = {};
    }

    if (Device != VK_NULL_HANDLE && PipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
        PipelineLayout = VK_NULL_HANDLE;
    }

    Pipeline = VK_NULL_HANDLE;
    CachedColor = VK_FORMAT_UNDEFINED;
    CachedDepth = VK_FORMAT_UNDEFINED;
    Buffers = nullptr;
    Scratch = nullptr;
    Pipelines = nullptr;
    Shaders = nullptr;
    Device = VK_NULL_HANDLE;
}
