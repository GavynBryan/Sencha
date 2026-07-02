#pragma once

#include "render/ViewportTargetCache.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <vulkan/vulkan.h>

class VulkanPipelineCache;
class VulkanDescriptorCache;

struct BloomParams
{
    float Threshold = 1.0f; // per-channel HDR threshold; only > this blooms
    float Intensity = 1.0f; // additive strength of the glow
    float Radius    = 1.0f; // blur spread (texel-step multiplier)
};

// Selection glow: bright-pass -> separable blur -> additive composite, run per viewport
// on the Phase 3 HDR targets. The bright HDR active wireframe (> 1.0) is the only thing
// that survives the threshold, so it glows while solid bodies do not. Samples through
// the engine's bindless image array (index in a push constant), so no per-pass
// descriptor sets are needed.
class EditorBloomPass
{
public:
    void Setup(const RendererServices& services);
    // Adds bloom to target.ColorImage in place. Expects the scene color in
    // SHADER_READ_ONLY on entry (after the scene pass); leaves it SHADER_READ_ONLY with
    // the glow composited on exit, ready for the UI's ImGui::Image.
    void Record(const FrameContext& frame,
                const ViewportTargetCache::RenderView& target,
                const BloomParams& params);
    void Teardown();

private:
    enum Pass { Bright = 0, Blur = 1, Composite = 2 };
    [[nodiscard]] VkPipeline EnsurePipeline(Pass pass, VkFormat colorFormat);

    VkDevice               Device = VK_NULL_HANDLE;
    VulkanShaderCache*     Shaders = nullptr;
    VulkanPipelineCache*   Pipelines = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;

    ShaderHandle VertShader;
    ShaderHandle BrightFrag;
    ShaderHandle BlurFrag;
    ShaderHandle CompositeFrag;

    VkPipelineLayout Layout = VK_NULL_HANDLE;
    VkPipeline       PassPipelines[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkFormat         CachedFormat = VK_FORMAT_UNDEFINED;
};
