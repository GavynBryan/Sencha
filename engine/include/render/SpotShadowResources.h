#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanImageService.h>

#include <vulkan/vulkan.h>

class SpotShadowResources
{
public:
    bool Setup(const RendererServices& services);
    void Teardown();

    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const { return SetLayout; }
    [[nodiscard]] VkDescriptorSet GetSet() const { return Set; }
    [[nodiscard]] VkImage GetImage() const;
    [[nodiscard]] VkImageView GetView() const;

    void TransitionForWrite(VkCommandBuffer commandBuffer);
    void TransitionForRead(VkCommandBuffer commandBuffer);

private:
    VulkanImageService* Images = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    ImageHandle Atlas;
    VkSampler Sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSet Set = VK_NULL_HANDLE;
    VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
};
