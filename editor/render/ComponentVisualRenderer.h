#pragma once

#include "../level/LevelScene.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class VulkanBufferService;
class VulkanFrameScratch;
class VulkanPipelineCache;
class VulkanShaderCache;

// Draws per-component editor visuals as wireframe at each entity's transform.
// Generic: it iterates the component-serializer registry and renders any
// EditorVisual a component advertises (today: Camera -> camera.glb). It names no
// component type. Mesh assets are imported once (glTF) and cached as edge lists.
// (08-... + EditorVisual hint; consumes IComponentSerializer::GetEditorVisual.)
class ComponentVisualRenderer
{
public:
    explicit ComponentVisualRenderer(LevelScene& scene);

    void Setup(const RendererServices& services);
    void DrawViewport(const FrameContext& frame, const EditorViewport& viewport);
    void Teardown();

private:
    struct LineVertex
    {
        Vec3d Position;
        Vec4 Color;
    };

    struct LinePushConstants
    {
        Mat4 ViewProjection;
    };

    // A mesh asset reduced to unique undirected edges in local space.
    struct MeshEdges
    {
        std::vector<Vec3d> Positions;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> Edges;
    };

    const MeshEdges& EdgesFor(std::string_view assetPath);

    LevelScene& Scene;
    std::unordered_map<std::string, MeshEdges> Cache;

    VulkanBufferService* Buffers = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkFormat CachedColor = VK_FORMAT_UNDEFINED;
    VkFormat CachedDepth = VK_FORMAT_UNDEFINED;
};
