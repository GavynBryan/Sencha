#pragma once

#include <graphics/vulkan/VulkanShaderCache.h>
#include <vulkan/vulkan.h>

struct EditorViewport;
struct RendererServices;
struct GridSettings;
class VulkanPipelineCache;

// Live look knobs, driven by editor.grid.* cvars (read once per frame by the render
// feature). Defaults match the prior hardcoded constants.
struct GridStyle
{
    float CellPx     = 14.0f; // target on-screen cell size (density)
    float Opacity    = 0.6f;  // line opacity
    float Brightness = 0.62f; // line color (gray level)
    float FadeStart  = 0.2f;  // horizon fade start, as a fraction of the reach
};

class GpuGridRenderer
{
public:
    void Setup(const RendererServices& services);
    void DrawViewport(VkCommandBuffer cmd,
                      const EditorViewport& viewport,
                      const GridSettings& gridSettings,
                      const GridStyle& style,
                      VkExtent2D targetExtent,
                      VkFormat colorFormat,
                      VkFormat depthFormat);
    void Teardown();

private:
    VkDevice          Device          = VK_NULL_HANDLE;
    VkPipelineLayout  PipelineLayout  = VK_NULL_HANDLE;
    VkPipeline        Pipeline        = VK_NULL_HANDLE;
    VkFormat          CachedColor     = VK_FORMAT_UNDEFINED;
    VkFormat          CachedDepth     = VK_FORMAT_UNDEFINED;

    ShaderHandle       VertexShader;
    ShaderHandle       FragmentShader;
    VulkanShaderCache* Shaders    = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
};
