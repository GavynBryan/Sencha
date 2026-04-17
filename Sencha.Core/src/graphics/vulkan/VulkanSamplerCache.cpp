#include <graphics/vulkan/VulkanSamplerCache.h>

#include <graphics/vulkan/VulkanDeviceService.h>

namespace
{
    // FNV-1a-ish combine — cheap and good enough for a cache of maybe ~16 samplers.
    inline void HashCombine(size_t& seed, size_t value)
    {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    }
}

size_t VulkanSamplerCache::DescHash::operator()(const SamplerDesc& d) const noexcept
{
    size_t h = 0;
    HashCombine(h, static_cast<size_t>(d.MinFilter));
    HashCombine(h, static_cast<size_t>(d.MagFilter));
    HashCombine(h, static_cast<size_t>(d.MipmapMode));
    HashCombine(h, static_cast<size_t>(d.AddressModeU));
    HashCombine(h, static_cast<size_t>(d.AddressModeV));
    HashCombine(h, static_cast<size_t>(d.AddressModeW));
    HashCombine(h, std::hash<float>{}(d.MaxAnisotropy));
    HashCombine(h, std::hash<float>{}(d.MaxLod));
    return h;
}

VulkanSamplerCache::VulkanSamplerCache(LoggingProvider& logging, VulkanDeviceService& device)
    : Log(logging.GetLogger<VulkanSamplerCache>())
    , Device(device.GetDevice())
{
    if (!device.IsValid())
    {
        Log.Error("VulkanSamplerCache: upstream device not valid");
        Device = VK_NULL_HANDLE;
    }
}

VulkanSamplerCache::~VulkanSamplerCache()
{
    for (auto& [_, sampler] : Cache)
    {
        if (sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Device, sampler, nullptr);
        }
    }
    Cache.clear();
}

VkSampler VulkanSamplerCache::Get(const SamplerDesc& desc)
{
    if (auto it = Cache.find(desc); it != Cache.end())
    {
        return it->second;
    }

    VkSampler sampler = CreateSampler(desc);
    if (sampler != VK_NULL_HANDLE)
    {
        Cache.emplace(desc, sampler);
    }
    return sampler;
}

VkSampler VulkanSamplerCache::CreateSampler(const SamplerDesc& desc)
{
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.minFilter = desc.MinFilter;
    info.magFilter = desc.MagFilter;
    info.mipmapMode = desc.MipmapMode;
    info.addressModeU = desc.AddressModeU;
    info.addressModeV = desc.AddressModeV;
    info.addressModeW = desc.AddressModeW;
    info.anisotropyEnable = desc.MaxAnisotropy > 0.0f ? VK_TRUE : VK_FALSE;
    info.maxAnisotropy = desc.MaxAnisotropy;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.minLod = 0.0f;
    info.maxLod = desc.MaxLod;
    info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    const VkResult result = vkCreateSampler(Device, &info, nullptr, &sampler);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateSampler failed ({})", static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return sampler;
}

VkSampler VulkanSamplerCache::GetLinearRepeat()
{
    SamplerDesc d{};
    d.MinFilter = VK_FILTER_LINEAR;
    d.MagFilter = VK_FILTER_LINEAR;
    d.MipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    return Get(d);
}

VkSampler VulkanSamplerCache::GetLinearClamp()
{
    SamplerDesc d{};
    d.MinFilter = VK_FILTER_LINEAR;
    d.MagFilter = VK_FILTER_LINEAR;
    d.MipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    d.AddressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    d.AddressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    d.AddressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return Get(d);
}

VkSampler VulkanSamplerCache::GetNearestRepeat()
{
    SamplerDesc d{};
    d.MinFilter = VK_FILTER_NEAREST;
    d.MagFilter = VK_FILTER_NEAREST;
    d.MipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    return Get(d);
}

VkSampler VulkanSamplerCache::GetNearestClamp()
{
    SamplerDesc d{};
    d.MinFilter = VK_FILTER_NEAREST;
    d.MagFilter = VK_FILTER_NEAREST;
    d.MipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    d.AddressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    d.AddressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    d.AddressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return Get(d);
}
