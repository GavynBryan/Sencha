#include "ComponentVisualRenderer.h"

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <shaders/kEditorLineFragSpv.h>
#include <shaders/kEditorLineVertSpv.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneSerializer.h>

#ifdef SENCHA_ENABLE_COOK
#include <assets/cook/MeshCook.h>
#endif

#include <cstddef>
#include <cstring>
#include <fstream>
#include <set>
#include <span>
#include <vector>

namespace
{
constexpr Vec4 kVisualColor{ 0.7f, 0.85f, 1.0f, 1.0f };

std::vector<std::byte> ReadFileBytes(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
        return {};
    const std::streamoff size = stream.tellg();
    if (size <= 0)
        return {};
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}
}

ComponentVisualRenderer::ComponentVisualRenderer(LevelScene& scene)
    : Scene(scene)
{
}

void ComponentVisualRenderer::Setup(const RendererServices& services)
{
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    Shaders = services.Shaders;
    Pipelines = services.Pipelines;
    Scratch = services.Scratch;
    Buffers = services.Buffers;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kEditorLineVertSpv, kEditorLineVertSpvWordCount, "Component visual vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kEditorLineFragSpv, kEditorLineFragSpvWordCount, "Component visual fragment");

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

const ComponentVisualRenderer::MeshEdges& ComponentVisualRenderer::EdgesFor(std::string_view assetPath)
{
    const std::string key(assetPath);
    if (const auto it = Cache.find(key); it != Cache.end())
        return it->second;

    MeshEdges edges; // empty on any failure — cached so we don't retry each frame

#ifdef SENCHA_ENABLE_COOK
    const std::vector<std::byte> bytes = ReadFileBytes(std::string(SENCHA_EDITOR_ASSET_DIR) + "/" + key);
    if (!bytes.empty())
    {
        std::vector<ImportedGltfMesh> meshes;
        if (ImportGltfMeshes(bytes, meshes))
        {
            std::set<std::pair<std::uint32_t, std::uint32_t>> unique;
            for (const ImportedGltfMesh& mesh : meshes)
            {
                const auto base = static_cast<std::uint32_t>(edges.Positions.size());
                for (const StaticMeshVertex& vertex : mesh.Geometry.Vertices)
                    edges.Positions.push_back(vertex.Position);

                const std::vector<std::uint32_t>& indices = mesh.Geometry.Indices;
                for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                {
                    const std::uint32_t tri[3] = {
                        base + indices[i], base + indices[i + 1], base + indices[i + 2] };
                    for (int e = 0; e < 3; ++e)
                    {
                        std::uint32_t a = tri[e];
                        std::uint32_t b = tri[(e + 1) % 3];
                        if (a > b)
                            std::swap(a, b);
                        unique.emplace(a, b);
                    }
                }
            }
            edges.Edges.assign(unique.begin(), unique.end());
        }
    }
#endif

    return Cache.emplace(key, std::move(edges)).first->second;
}

void ComponentVisualRenderer::DrawViewport(const FrameContext& frame, const EditorViewport& viewport)
{
    if (PipelineLayout == VK_NULL_HANDLE || Scratch == nullptr || Buffers == nullptr || frame.DepthFormat == VK_FORMAT_UNDEFINED)
        return;

    if (Pipeline == VK_NULL_HANDLE || CachedColor != frame.TargetFormat || CachedDepth != frame.DepthFormat)
    {
        GraphicsPipelineDesc desc{};
        desc.VertexShader = VertexShader;
        desc.FragmentShader = FragmentShader;
        desc.Layout = PipelineLayout;
        desc.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        desc.VertexBindings = { { 0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX } };
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

    const Registry& registry = Scene.GetRegistry();
    const auto& serializers = GetComponentSerializerEntries();

    std::vector<LineVertex> vertices;
    for (EntityId entity : Scene.GetAllEntities())
    {
        const Transform3f* transform = Scene.TryGetTransform(entity);
        if (transform == nullptr)
            continue;

        for (const auto& serializer : serializers)
        {
            if (!serializer->HasComponent(entity, registry))
                continue;
            const std::optional<EditorVisual> visual = serializer->GetEditorVisual();
            if (!visual.has_value() || visual->VisualKind != EditorVisual::Kind::Mesh)
                continue;

            const MeshEdges& edges = EdgesFor(visual->AssetPath);
            for (const auto& [a, b] : edges.Edges)
            {
                vertices.push_back(LineVertex{ .Position = transform->TransformPoint(edges.Positions[a]), .Color = kVisualColor });
                vertices.push_back(LineVertex{ .Position = transform->TransformPoint(edges.Positions[b]), .Color = kVisualColor });
            }
        }
    }

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
    scissor.offset = { static_cast<int32_t>(viewport.RegionMin.x), static_cast<int32_t>(viewport.RegionMin.y) };
    scissor.extent = { static_cast<uint32_t>(vkViewport.width), static_cast<uint32_t>(vkViewport.height) };

    const CameraRenderData renderData = viewport.BuildRenderData();
    const LinePushConstants push{ .ViewProjection = renderData.ViewProjection.Transposed() };

    VkBuffer vertexBuffer = Buffers->GetBuffer(allocation.Buffer);
    VkDeviceSize vertexOffset = allocation.Offset;

    vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline);
    vkCmdSetViewport(frame.Cmd, 0, 1, &vkViewport);
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);
    vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
    vkCmdPushConstants(frame.Cmd, PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(frame.Cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
}

void ComponentVisualRenderer::Teardown()
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
