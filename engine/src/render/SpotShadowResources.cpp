#include <render/SpotShadowResources.h>

#include <graphics/vulkan/VulkanBarriers.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <render/RenderLight.h>

bool SpotShadowResources::Setup(const RendererServices& services)
{
    Images = services.Images;
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    if (Images == nullptr || Device == VK_NULL_HANDLE)
        return false;

    Atlas = Images->Create(ImageCreateInfo{
        .Extent = { kSpotShadowAtlasExtent, kSpotShadowAtlasExtent },
        .Format = VK_FORMAT_D16_UNORM,
        .Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .DebugName = "Spot shadow atlas",
    });
    if (!Atlas.IsValid())
        return false;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(Device, &samplerInfo, nullptr, &Sampler) != VK_SUCCESS)
    {
        Teardown();
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(Device, &layoutInfo, nullptr, &SetLayout) != VK_SUCCESS)
    {
        Teardown();
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(Device, &poolInfo, nullptr, &Pool) != VK_SUCCESS)
    {
        Teardown();
        return false;
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = Pool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &SetLayout;
    if (vkAllocateDescriptorSets(Device, &allocateInfo, &Set) != VK_SUCCESS)
    {
        Teardown();
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = Sampler;
    imageInfo.imageView = Images->GetView(Atlas);
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = Set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);
    return true;
}

void SpotShadowResources::Teardown()
{
    Set = VK_NULL_HANDLE;
    if (Pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(Device, Pool, nullptr);
    Pool = VK_NULL_HANDLE;
    if (SetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
    SetLayout = VK_NULL_HANDLE;
    if (Sampler != VK_NULL_HANDLE)
        vkDestroySampler(Device, Sampler, nullptr);
    Sampler = VK_NULL_HANDLE;
    if (Images != nullptr && Atlas.IsValid())
        Images->Destroy(Atlas);
    Atlas = {};
    Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    Images = nullptr;
    Device = VK_NULL_HANDLE;
}

bool SpotShadowResources::IsValid() const
{
    return Atlas.IsValid()
        && Sampler != VK_NULL_HANDLE
        && SetLayout != VK_NULL_HANDLE
        && Set != VK_NULL_HANDLE;
}

VkImage SpotShadowResources::GetImage() const
{
    return Images != nullptr ? Images->GetImage(Atlas) : VK_NULL_HANDLE;
}

VkImageView SpotShadowResources::GetView() const
{
    return Images != nullptr ? Images->GetView(Atlas) : VK_NULL_HANDLE;
}

void SpotShadowResources::TransitionForWrite(VkCommandBuffer commandBuffer)
{
    VulkanBarriers::ImageTransition transition{};
    transition.Image = GetImage();
    transition.OldLayout = Layout;
    transition.NewLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    transition.SrcStage = Layout == VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
        : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    transition.DstStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    transition.SrcAccess = Layout == VK_IMAGE_LAYOUT_UNDEFINED
        ? 0
        : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    transition.DstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                         | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    transition.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VulkanBarriers::TransitionImage(commandBuffer, transition);
    Layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
}

void SpotShadowResources::TransitionForRead(VkCommandBuffer commandBuffer)
{
    VulkanBarriers::ImageTransition transition{};
    transition.Image = GetImage();
    transition.OldLayout = Layout;
    transition.NewLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    transition.SrcStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
                        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    transition.DstStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    transition.SrcAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    transition.DstAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    transition.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VulkanBarriers::TransitionImage(commandBuffer, transition);
    Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
}
