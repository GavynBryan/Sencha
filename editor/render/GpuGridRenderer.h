#pragma once

#include <graphics/vulkan/VulkanShaderCache.h>
#include <vulkan/vulkan.h>

struct EditorViewport;
struct RendererServices;
class VulkanPipelineCache;

class GpuGridRenderer
{
public:
    void Setup(const RendererServices& services);
    void DrawViewport(VkCommandBuffer cmd,
                      const EditorViewport& viewport,
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
