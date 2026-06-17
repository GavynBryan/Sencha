#include "SelectionRenderer.h"

#include "../editmodes/ManipulatorSession.h"
#include "../meshedit/MeshElements.h"

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kEditorLineFragSpv.h>
#include <shaders/kEditorLineVertSpv.h>

#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

SelectionRenderer::SelectionRenderer(LevelScene& scene, SelectionService& selection, ManipulatorSession& session)
    : Scene(scene)
    , Selection(selection)
    , Session(session)
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

    const std::span<const SelectableRef> selection = Selection.GetSelection();
    if (selection.empty())
        return;

    std::vector<LineVertex> vertices;
    vertices.reserve(selection.size() * 32);
    for (SelectableRef selected : selection)
    {
        if (!selected.IsValid() || selected.Registry != Scene.GetRegistry().Id)
            continue;

        const BrushMesh* mesh = Scene.TryGetBrushMesh(selected.Entity);
        const Transform3f* transform = Scene.TryGetTransform(selected.Entity);
        if (mesh == nullptr || transform == nullptr)
            continue;

        AppendBrushMesh(vertices, *mesh, *transform, Vec4(1.0f, 1.0f, 0.0f, 1.0f));

        if (selected.IsFace())
        {
            if (const std::optional<FaceElement> face = MeshElements::TryGetFace(*mesh, *transform, selected.ElementId))
                AppendFace(vertices, *face, Vec4(1.0f, 0.4f, 0.1f, 1.0f));
        }
        else if (selected.IsEdge())
        {
            if (const std::optional<EdgeElement> edge = MeshElements::TryGetEdge(*mesh, *transform, selected.ElementId))
                AppendEdge(vertices, *edge, Vec4(0.2f, 0.9f, 1.0f, 1.0f));
        }
        else if (selected.IsVertex())
        {
            if (const std::optional<VertexElement> vertex = MeshElements::TryGetVertex(*mesh, *transform, selected.ElementId))
                AppendVertex(vertices, *vertex, Vec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }

    // Manipulators draw themselves; the renderer just converts their line list and
    // never assumes a gizmo shape. (08-select-tool-v2.md)
    AppendManipulators(vertices, viewport);

    if (vertices.empty())
        return;

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

void SelectionRenderer::AppendBrushMesh(std::vector<LineVertex>& vertices,
                                        const BrushMesh& mesh,
                                        const Transform3f& transform,
                                        const Vec4& color) const
{
    for (const BrushFace& face : mesh.Faces)
    {
        const std::size_t n = face.Loop.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            const Vec3d a = transform.TransformPoint(mesh.Vertices[face.Loop[i]].Position);
            const Vec3d b = transform.TransformPoint(mesh.Vertices[face.Loop[(i + 1) % n]].Position);
            vertices.push_back(LineVertex{ .Position = a, .Color = color });
            vertices.push_back(LineVertex{ .Position = b, .Color = color });
        }
    }
}

void SelectionRenderer::AppendFace(std::vector<LineVertex>& vertices,
                                   const FaceElement& face,
                                   const Vec4& color) const
{
    for (size_t i = 0; i < face.Corners.size(); ++i)
    {
        const Vec3d& start = face.Corners[i];
        const Vec3d& end = face.Corners[(i + 1) % face.Corners.size()];
        vertices.push_back(LineVertex{ .Position = start, .Color = color });
        vertices.push_back(LineVertex{ .Position = end, .Color = color });
    }
}

void SelectionRenderer::AppendEdge(std::vector<LineVertex>& vertices,
                                   const EdgeElement& edge,
                                   const Vec4& color) const
{
    vertices.push_back(LineVertex{ .Position = edge.A, .Color = color });
    vertices.push_back(LineVertex{ .Position = edge.B, .Color = color });
}

void SelectionRenderer::AppendVertex(std::vector<LineVertex>& vertices,
                                     const VertexElement& vertex,
                                     const Vec4& color) const
{
    constexpr float radius = 0.05f;
    const Vec3d p = vertex.Position;
    vertices.push_back(LineVertex{ .Position = p + Vec3d(-radius, 0.0f, 0.0f), .Color = color });
    vertices.push_back(LineVertex{ .Position = p + Vec3d(radius, 0.0f, 0.0f), .Color = color });
    vertices.push_back(LineVertex{ .Position = p + Vec3d(0.0f, -radius, 0.0f), .Color = color });
    vertices.push_back(LineVertex{ .Position = p + Vec3d(0.0f, radius, 0.0f), .Color = color });
    vertices.push_back(LineVertex{ .Position = p + Vec3d(0.0f, 0.0f, -radius), .Color = color });
    vertices.push_back(LineVertex{ .Position = p + Vec3d(0.0f, 0.0f, radius), .Color = color });
}

void SelectionRenderer::AppendManipulators(std::vector<LineVertex>& vertices,
                                           const EditorViewport& viewport) const
{
    // Each active manipulator draws itself; the renderer just converts the line
    // list. Whatever the manipulators are (translate arrows now, bounds handles /
    // rotate rings / scale later), this code is unchanged.
    ManipulatorVisual visual;
    Session.BuildVisuals(viewport, visual);
    for (const ManipulatorVisual::Line& line : visual.Lines)
    {
        vertices.push_back(LineVertex{ .Position = line.A, .Color = line.Color });
        vertices.push_back(LineVertex{ .Position = line.B, .Color = line.Color });
    }
}
