#pragma once

#include "../level/BrushGeometry.h"
#include "../level/LevelScene.h"
#include "../render/PreviewBuffer.h"
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

    void SetPreviewBuffer(PreviewBuffer* preview);

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
    // Draws the actual mesh edges (every face loop), transformed to world — what
    // renders non-box brushes correctly. AppendBrush stays for the create-drag
    // preview box.
    void AppendBrushMesh(std::vector<LineVertex>& vertices,
                         const BrushMesh& mesh,
                         const Transform3f& transform,
                         const Vec4& color) const;

    LevelScene& Scene;
    PreviewBuffer* Preview = nullptr;
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
