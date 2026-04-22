#include "SelectionRenderer.h"

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kEditorLineFragSpv.h>
#include <shaders/kEditorLineVertSpv.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

SelectionRenderer::SelectionRenderer(LevelScene& scene, SelectionService& selection)
    : Scene(scene)
    , Selection(selection)
{
}

void SelectionRenderer::Setup(const RendererServices& services)
{
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    Shaders = services.Shaders;
    Pipelines = services.Pipelines;
    Scratch = services.Scratch;
    Buffers = services.Buffers;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kEditorLineVertSpv, kEditorLineVertSpvWordCount, "Selection line vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kEditorLineFragSpv, kEditorLineFragSpvWordCount, "Selection line fragment");

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(LinePushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout);
}

void SelectionRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    if (PipelineLayout == VK_NULL_HANDLE || Scratch == nullptr || Buffers == nullptr || frame.DepthFormat == VK_FORMAT_UNDEFINED)
        return;

    if (Pipeline == VK_NULL_HANDLE
        || CachedColor != frame.TargetFormat
        || CachedDepth != frame.DepthFormat)
    {
        GraphicsPipelineDesc desc{};
        desc.VertexShader = VertexShader;
        desc.FragmentShader = FragmentShader;
        desc.Layout = PipelineLayout;
        desc.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        desc.VertexBindings = {
            { 0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX },
        };
        desc.VertexAttributes = {
            { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, Position) },
            { 1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LineVertex, Color) },
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

    const SelectableRef selected = Selection.GetPrimarySelection();
    if (!selected.IsValid() || selected.Registry != Scene.GetRegistry().Id)
        return;

    const Transform3f* transform = Scene.TryGetTransform(selected.Entity);
    const BrushComponent* brush = Scene.TryGetBrush(selected.Entity);
    if (transform == nullptr || brush == nullptr)
        return;

    std::vector<LineVertex> vertices;
    vertices.reserve(24);
    AppendBrush(vertices, *transform, *brush, Vec4(1.0f, 1.0f, 0.0f, 1.0f));

    const VkDeviceSize byteCount = sizeof(LineVertex) * vertices.size();
    const auto allocation = Scratch->AllocateVertex(byteCount);
    if (!allocation.IsValid())
        return;

    std::memcpy(allocation.Mapped, vertices.data(), static_cast<size_t>(byteCount));

    VkViewport vkViewport{};
    vkViewport.x = viewport.RegionMin.x;
    vkViewport.y = viewport.RegionMin.y;
    vkViewport.width = viewport.RegionMax.x - viewport.RegionMin.x;
    vkViewport.height = viewport.RegionMax.y - viewport.RegionMin.y;
    vkViewport.minDepth = 0.0f;
    vkViewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {
        static_cast<int32_t>(viewport.RegionMin.x),
        static_cast<int32_t>(viewport.RegionMin.y),
    };
    scissor.extent = {
        static_cast<uint32_t>(vkViewport.width),
        static_cast<uint32_t>(vkViewport.height),
    };

    const CameraRenderData renderData = viewport.BuildRenderData();
    const LinePushConstants push{
        .ViewProjection = renderData.ViewProjection.Transposed(),
    };

    VkBuffer vertexBuffer = Buffers->GetBuffer(allocation.Buffer);
    VkDeviceSize vertexOffset = allocation.Offset;

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
    vkCmdSetViewport(frame.Cmd, 0, 1, &vkViewport);
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);
    vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
    vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(frame.Cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
}

void SelectionRenderer::Teardown()
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

void SelectionRenderer::AppendBrush(std::vector<LineVertex>& vertices,
                                    const Transform3f& transform,
                                    const BrushComponent& brush,
                                    const Vec4& color) const
{
    const std::array<Vec3d, 8> corners = {
        transform.TransformPoint(Vec3d(-brush.HalfExtents.X, -brush.HalfExtents.Y, -brush.HalfExtents.Z)),
        transform.TransformPoint(Vec3d(brush.HalfExtents.X, -brush.HalfExtents.Y, -brush.HalfExtents.Z)),
        transform.TransformPoint(Vec3d(brush.HalfExtents.X, brush.HalfExtents.Y, -brush.HalfExtents.Z)),
        transform.TransformPoint(Vec3d(-brush.HalfExtents.X, brush.HalfExtents.Y, -brush.HalfExtents.Z)),
        transform.TransformPoint(Vec3d(-brush.HalfExtents.X, -brush.HalfExtents.Y, brush.HalfExtents.Z)),
        transform.TransformPoint(Vec3d(brush.HalfExtents.X, -brush.HalfExtents.Y, brush.HalfExtents.Z)),
        transform.TransformPoint(Vec3d(brush.HalfExtents.X, brush.HalfExtents.Y, brush.HalfExtents.Z)),
        transform.TransformPoint(Vec3d(-brush.HalfExtents.X, brush.HalfExtents.Y, brush.HalfExtents.Z)),
    };

    constexpr std::array<std::pair<int, int>, 12> edges = {{
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    }};

    for (const auto& [start, end] : edges)
    {
        vertices.push_back(LineVertex{ .Position = corners[start], .Color = color });
        vertices.push_back(LineVertex{ .Position = corners[end], .Color = color });
    }
}
