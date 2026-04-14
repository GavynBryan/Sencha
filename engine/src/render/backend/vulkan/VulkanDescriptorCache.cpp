#include <render/backend/vulkan/VulkanDescriptorCache.h>

#include <render/backend/vulkan/VulkanDeviceService.h>

namespace
{
    constexpr uint32_t kFrameUboBinding = 0;
    constexpr uint32_t kBindlessImageBinding = 1;

    bool PushConstantsEqual(const std::vector<VkPushConstantRange>& a,
                            const std::vector<VkPushConstantRange>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].stageFlags != b[i].stageFlags) return false;
            if (a[i].offset != b[i].offset) return false;
            if (a[i].size != b[i].size) return false;
        }
        return true;
    }
}

VulkanDescriptorCache::VulkanDescriptorCache(LoggingProvider& logging,
                                             VulkanDeviceService& device,
                                             VulkanBufferService& buffers,
                                             VulkanImageService& images)
    : Log(logging.GetLogger<VulkanDescriptorCache>())
    , Device(device.GetDevice())
    , Buffers(&buffers)
    , Images(&images)
{
    if (!device.IsValid() || !buffers.IsValid() || !images.IsValid())
    {
        Log.Error("Cannot create VulkanDescriptorCache: upstream services not valid");
        return;
    }

    if (!CreatePoolAndLayout()) return;
    if (!AllocateSet()) return;

    Valid = true;
}

VulkanDescriptorCache::~VulkanDescriptorCache()
{
    for (auto& entry : PipelineLayouts)
    {
        if (entry.Layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(Device, entry.Layout, nullptr);
        }
    }
    PipelineLayouts.clear();

    if (SetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(Device, SetLayout, nullptr);
        SetLayout = VK_NULL_HANDLE;
    }

    if (Pool != VK_NULL_HANDLE)
    {
        // Freeing the pool destroys every set allocated from it.
        vkDestroyDescriptorPool(Device, Pool, nullptr);
        Pool = VK_NULL_HANDLE;
    }
}

bool VulkanDescriptorCache::CreatePoolAndLayout()
{
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kBindlessImageCapacity;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(Device, &poolInfo, nullptr, &Pool) != VK_SUCCESS)
    {
        Log.Error("vkCreateDescriptorPool failed");
        return false;
    }

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = kFrameUboBinding;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = kBindlessImageBinding;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = kBindlessImageCapacity;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorBindingFlags bindingFlags[2]{};
    bindingFlags[0] = 0;
    bindingFlags[1] =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = 2;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    if (vkCreateDescriptorSetLayout(Device, &layoutInfo, nullptr, &SetLayout) != VK_SUCCESS)
    {
        Log.Error("vkCreateDescriptorSetLayout failed");
        return false;
    }

    return true;
}

bool VulkanDescriptorCache::AllocateSet()
{
    // Variable-count tail: we allocate the full capacity up front so every
    // slot is addressable even though partially-bound leaves them empty.
    const uint32_t variableCount = kBindlessImageCapacity;
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableInfo{};
    variableInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    variableInfo.descriptorSetCount = 1;
    variableInfo.pDescriptorCounts = &variableCount;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = &variableInfo;
    allocInfo.descriptorPool = Pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &SetLayout;

    if (vkAllocateDescriptorSets(Device, &allocInfo, &Set) != VK_SUCCESS)
    {
        Log.Error("vkAllocateDescriptorSets failed");
        return false;
    }

    return true;
}

VkPipelineLayout VulkanDescriptorCache::GetPipelineLayout(
    const std::vector<VkPushConstantRange>& pushConstants)
{
    if (!Valid) return VK_NULL_HANDLE;

    for (const auto& entry : PipelineLayouts)
    {
        if (PushConstantsEqual(entry.PushConstants, pushConstants))
        {
            return entry.Layout;
        }
    }

    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &SetLayout;
    info.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
    info.pPushConstantRanges = pushConstants.data();

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(Device, &info, nullptr, &layout) != VK_SUCCESS)
    {
        Log.Error("vkCreatePipelineLayout failed");
        return VK_NULL_HANDLE;
    }

    PipelineLayouts.push_back({ pushConstants, layout });
    return layout;
}

VkPipelineLayout VulkanDescriptorCache::GetDefaultPipelineLayout()
{
    static const std::vector<VkPushConstantRange> kNone;
    return GetPipelineLayout(kNone);
}

void VulkanDescriptorCache::SetFrameUniformBuffer(BufferHandle buffer, VkDeviceSize range)
{
    if (!Valid) return;

    VkBuffer vkBuf = Buffers->GetBuffer(buffer);
    if (vkBuf == VK_NULL_HANDLE)
    {
        Log.Error("SetFrameUniformBuffer: invalid BufferHandle");
        return;
    }

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = vkBuf;
    bufInfo.offset = 0;
    bufInfo.range = range;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = Set;
    write.dstBinding = kFrameUboBinding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);
}

BindlessImageIndex VulkanDescriptorCache::RegisterSampledImage(ImageHandle image, VkSampler sampler)
{
    if (!Valid) return {};

    if (Images->GetView(image) == VK_NULL_HANDLE)
    {
        Log.Error("RegisterSampledImage: invalid ImageHandle");
        return {};
    }
    if (sampler == VK_NULL_HANDLE)
    {
        Log.Error("RegisterSampledImage: null sampler");
        return {};
    }

    const BindlessKey key{ image.Id, sampler };
    if (auto it = BindlessLookup.find(key); it != BindlessLookup.end())
    {
        return it->second;
    }

    uint32_t slot;
    if (!BindlessFreeSlots.empty())
    {
        slot = BindlessFreeSlots.back();
        BindlessFreeSlots.pop_back();
    }
    else
    {
        if (BindlessNextSlot >= kBindlessImageCapacity)
        {
            Log.Error("Bindless image capacity ({}) exhausted", kBindlessImageCapacity);
            return {};
        }
        slot = BindlessNextSlot++;
    }

    WriteBindlessSlot(slot, image, sampler);

    const BindlessImageIndex index{ slot };
    BindlessLookup.emplace(key, index);
    return index;
}

void VulkanDescriptorCache::UnregisterSampledImage(BindlessImageIndex index)
{
    if (!index.IsValid()) return;

    for (auto it = BindlessLookup.begin(); it != BindlessLookup.end(); ++it)
    {
        if (it->second == index)
        {
            BindlessFreeSlots.push_back(index.Value);
            BindlessLookup.erase(it);
            return;
        }
    }
}

void VulkanDescriptorCache::WriteBindlessSlot(uint32_t slot, ImageHandle image, VkSampler sampler)
{
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = sampler;
    imgInfo.imageView = Images->GetView(image);
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = Set;
    write.dstBinding = kBindlessImageBinding;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(Device, 1, &write, 0, nullptr);
}
