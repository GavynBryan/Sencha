#pragma once

#include <graphics/vulkan/PipelineVariantSet.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <cstdint>
#include <span>

class VulkanDescriptorCache;

// What every screen-space pass shares: the bufferless triangle, and a pipeline
// layout of the bindless image set at set 0 plus one fragment push block. A
// pass brings its fragment shaders and decides where they draw; it holds the
// pipelines this builds, since they belong to the pipeline cache.
class FullscreenPass
{
public:
    // False leaves the pass inert: BuildPipeline returns null.
    [[nodiscard]] bool Setup(const RendererServices& services, std::uint32_t pushConstantBytes);
    void Teardown();

    [[nodiscard]] VkPipeline BuildPipeline(ShaderHandle fragmentShader, const AttachmentFormatKey& formats,
                                           const ColorBlendAttachmentDesc& blend = {},
                                           std::span<const ShaderSpecializationConstant> specialization = {}) const;

    // Covers `extent`, inside a rendering scope the caller has open. `push` is
    // the whole block declared at Setup.
    void Draw(VkCommandBuffer cmd, VkExtent2D extent, VkPipeline pipeline, const void* push) const;
    // The same triangle, shaded only inside `scissor`.
    void Draw(VkCommandBuffer cmd, VkExtent2D extent, VkRect2D scissor, VkPipeline pipeline, const void* push) const;

private:
    VkDevice Device = VK_NULL_HANDLE;
    VulkanShaderCache* Shaders = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;

    ShaderHandle VertexShader;
    VkPipelineLayout Layout = VK_NULL_HANDLE;
    std::uint32_t PushConstantBytes = 0;
};
