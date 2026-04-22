#pragma once

#include "../level/BrushGeometry.h"
#include "../level/LevelScene.h"
#include "../selection/SelectionService.h"
#include "../viewport/EditorViewport.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <vector>

class VulkanBufferService;
class VulkanFrameScratch;
class VulkanPipelineCache;
class VulkanShaderCache;

class SelectionRenderer
{
public:
    SelectionRenderer(LevelScene& scene, SelectionService& selection);

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

    void AppendBrush(std::vector<LineVertex>& vertices,
                     const BrushState& brush,
                     const Vec4& color) const;
    void AppendFace(std::vector<LineVertex>& vertices,
                    const BrushFaceGeometry& face,
                    const Vec4& color) const;

    LevelScene& Scene;
    SelectionService& Selection;
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
