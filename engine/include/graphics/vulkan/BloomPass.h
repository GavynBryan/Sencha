#pragma once

#include <graphics/vulkan/FullscreenPass.h>
#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <cstdint>

struct BloomParams
{
    float Threshold = 1.0f;
    float Intensity = 1.0f;
    float Radius = 1.0f;
};

struct BloomPlane
{
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    RenderTargetAccess* Access = nullptr;
    std::uint32_t Bindless = UINT32_MAX;
    VkExtent2D Extent{};
};

// Light above a threshold, blurred and added back. The post effect chain and the
// level editor's selection glow both draw through it.
class BloomPass
{
public:
    [[nodiscard]] bool Setup(const RendererServices& services);
    void Teardown();

    // Outside any scope. Leaves the blurred light of `source` in `glow`.
    void Blur(const FrameContext& frame, std::uint32_t source, VkExtent2D sourceExtent, const BloomPlane& glow,
              const BloomPlane& scratch, const BloomParams& params);

    // Inside the target's scope. Adds `glow` over `scene`, or over what the target
    // holds when `scene` is UINT32_MAX.
    void Composite(const FrameContext& frame, std::uint32_t glow, std::uint32_t scene, float intensity);

private:
    enum Step : std::size_t { Bright, Spread, Replace, Over, StepCount };
    [[nodiscard]] bool EnsurePipelines(VkFormat format);

    VulkanShaderCache* Shaders = nullptr;
    FullscreenPass Fullscreen;
    ShaderHandle BrightFrag;
    ShaderHandle BlurFrag;
    ShaderHandle CompositeFrag;
    PipelineVariantSet<StepCount, VkFormat> Pipelines;
};
