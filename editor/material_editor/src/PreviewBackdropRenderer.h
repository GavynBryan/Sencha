#pragma once

#include <graphics/vulkan/VulkanShaderCache.h>
#include <math/Vec.h>
#include <vulkan/vulkan.h>

struct RendererServices;
class VulkanPipelineCache;

// Live look knobs, driven by editor.preview.backdrop.* cvars (read once per
// frame by the composition root and pushed onto the render feature).
struct PreviewBackdropStyle
{
    // Linear RGB (the cvar's #1e8fff sRGB default); w = intensity.
    Vec4 LineColor{ 0.013f, 0.274f, 1.0f, 1.6f };
    float CellPx = 64.0f; // grid cell size in pixels
    float GlowPx = 1.0f;  // halo sigma in pixels
};

// Fills the material preview target with black plus a screen-space grid of
// glowing lines, drawn before the mesh so it replaces the clear color. Depth
// is untouched; the previewed mesh renders over it.
class PreviewBackdropRenderer
{
public:
    void Setup(const RendererServices& services);
    void Draw(VkCommandBuffer cmd,
              VkExtent2D targetExtent,
              VkFormat colorFormat,
              VkFormat depthFormat,
              const PreviewBackdropStyle& style);
    void Teardown();

private:
    VkDevice             Device         = VK_NULL_HANDLE;
    VkPipelineLayout     PipelineLayout = VK_NULL_HANDLE;
    VkPipeline           Pipeline       = VK_NULL_HANDLE;
    VkFormat             CachedColor    = VK_FORMAT_UNDEFINED;
    VkFormat             CachedDepth    = VK_FORMAT_UNDEFINED;

    ShaderHandle         VertexShader;
    ShaderHandle         FragmentShader;
    VulkanShaderCache*   Shaders   = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
};
