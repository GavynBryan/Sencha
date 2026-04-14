#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>

class VulkanDeviceService;

//=============================================================================
// VulkanSamplerCache
//
// Deduplicates VkSampler objects by their descriptor. Samplers are pure,
// immutable state objects — there is no reason to ever create two identical
// ones. Callers describe what they want with SamplerDesc and receive a
// cached VkSampler handle that is owned by the cache.
//
// VkSampler is returned as a raw handle (not an opaque generational id) on
// purpose: samplers outlive any individual caller, are shared across draws,
// and get passed straight into descriptor writes. Wrapping them would add
// friction without any safety benefit.
//
// The cache owns every sampler it hands out and destroys them at service
// teardown. Do not vkDestroySampler on anything returned from Get().
//=============================================================================
struct SamplerDesc
{
    VkFilter MinFilter = VK_FILTER_LINEAR;
    VkFilter MagFilter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode MipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode AddressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode AddressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerAddressMode AddressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    float MaxAnisotropy = 0.0f; // 0 disables anisotropy
    float MaxLod = VK_LOD_CLAMP_NONE;

    bool operator==(const SamplerDesc&) const = default;
};

class VulkanSamplerCache : public IService
{
public:
    VulkanSamplerCache(LoggingProvider& logging, VulkanDeviceService& device);
    ~VulkanSamplerCache() override;

    VulkanSamplerCache(const VulkanSamplerCache&) = delete;
    VulkanSamplerCache& operator=(const VulkanSamplerCache&) = delete;
    VulkanSamplerCache(VulkanSamplerCache&&) = delete;
    VulkanSamplerCache& operator=(VulkanSamplerCache&&) = delete;

    [[nodiscard]] bool IsValid() const { return Device != VK_NULL_HANDLE; }

    // Returns a cached sampler matching `desc`, creating one if needed.
    [[nodiscard]] VkSampler Get(const SamplerDesc& desc);

    // Common presets.
    [[nodiscard]] VkSampler GetLinearRepeat();
    [[nodiscard]] VkSampler GetLinearClamp();
    [[nodiscard]] VkSampler GetNearestRepeat();
    [[nodiscard]] VkSampler GetNearestClamp();

private:
    struct DescHash
    {
        size_t operator()(const SamplerDesc& d) const noexcept;
    };

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    std::unordered_map<SamplerDesc, VkSampler, DescHash> Cache;

    [[nodiscard]] VkSampler CreateSampler(const SamplerDesc& desc);
};
