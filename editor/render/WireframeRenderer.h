#pragma once

#include "../level/LevelScene.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <vector>

class VulkanBufferService;
class VulkanDeviceService;
class VulkanFrameScratch;
class VulkanPipelineCache;
class VulkanShaderCache;

class WireframeRenderer
{
public:
    explicit WireframeRenderer(LevelScene& scene);

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

    void AppendCube(std::vector<LineVertex>& vertices,
                    const Transform3f& transform,
                    const CubePrimitive& cube,
                    const Vec4& color) const;

    LevelScene& Scene;
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
