#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <render/backend/vulkan/VulkanBufferService.h>
#include <render/backend/vulkan/VulkanImageService.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

class VulkanDeviceService;

//=============================================================================
// VulkanDescriptorCache
//
// Owns the one global descriptor set every Sencha pipeline binds to, plus
// the pipeline layouts built on top of it. The set is composed of:
//
//   Binding 0: Uniform buffer (dynamic offset)
//       Per-frame view/camera/etc. data. Point this at a buffer once via
//       SetFrameUniformBuffer(); each draw supplies its own dynamic offset
//       into that buffer. The buffer can be the frame scratch allocator
//       (step 8) or any UBO the caller manages.
//
//   Binding 1: Bindless combined-image-sampler array
//       A large descriptor array where each ImageHandle gets a stable
//       slot. Shaders sample via integer index rather than rebinding per
//       draw. Update-after-bind + partially-bound so the set never needs
//       to be rebuilt.
//
// The descriptor cache deals in Vulkan images, not Sencha "textures". A
// higher-layer resource/asset system is what will compose an ImageHandle
// with a sampler into whatever engine-level Texture type gameplay sees.
//
// Pipeline layouts are cached by push-constant-range signature. Every
// layout the cache returns uses the single global set layout above, so
// any pipeline that wants the bindless set just goes through the cache.
//=============================================================================

struct BindlessImageIndex
{
    uint32_t Value = UINT32_MAX;

    [[nodiscard]] bool IsValid() const { return Value != UINT32_MAX; }
    bool operator==(const BindlessImageIndex&) const = default;
};

class VulkanDescriptorCache : public IService
{
public:
    // Capacity of the bindless sampled-image array. Any single registered
    // (image, sampler) pair consumes one slot. 1024 is generous for a 2D
    // renderer and comfortably under typical maxDescriptorSetSampledImages.
    static constexpr uint32_t kBindlessImageCapacity = 1024;

    VulkanDescriptorCache(LoggingProvider& logging,
                          VulkanDeviceService& device,
                          VulkanBufferService& buffers,
                          VulkanImageService& images);
    ~VulkanDescriptorCache() override;

    VulkanDescriptorCache(const VulkanDescriptorCache&) = delete;
    VulkanDescriptorCache& operator=(const VulkanDescriptorCache&) = delete;
    VulkanDescriptorCache(VulkanDescriptorCache&&) = delete;
    VulkanDescriptorCache& operator=(VulkanDescriptorCache&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // -- Set layout / pipeline layout ---------------------------------------

    [[nodiscard]] VkDescriptorSetLayout GetSetLayout() const { return SetLayout; }

    // Returns the one descriptor set that every pipeline binds to set index 0.
    [[nodiscard]] VkDescriptorSet GetSet() const { return Set; }

    // Returns a pipeline layout backed by GetSetLayout() with the supplied
    // push-constant ranges. Identical push-constant signatures return the
    // same cached VkPipelineLayout.
    [[nodiscard]] VkPipelineLayout GetPipelineLayout(
        const std::vector<VkPushConstantRange>& pushConstants);

    // Convenience: pipeline layout with no push constants.
    [[nodiscard]] VkPipelineLayout GetDefaultPipelineLayout();

    // -- Frame UBO ----------------------------------------------------------
    //
    // Point binding 0 at `buffer` covering [0, range). Per-frame or per-draw
    // data is then addressed via a dynamic offset at vkCmdBindDescriptorSets
    // time. Call once when the frame UBO backing buffer is created; safe to
    // call again if the backing buffer changes.
    void SetFrameUniformBuffer(BufferHandle buffer, VkDeviceSize range);

    // -- Bindless sampled images --------------------------------------------
    //
    // Assign a bindless slot to (image, sampler). Re-registering the same
    // pair returns the same slot -- cheap idempotent call. The returned
    // index is what gameplay code writes into per-sprite instance data.
    [[nodiscard]] BindlessImageIndex RegisterSampledImage(ImageHandle image, VkSampler sampler);

    // Releases a slot. The descriptor write isn't actively revoked -- it
    // just becomes a dangling slot that will be overwritten the next time
    // RegisterSampledImage allocates a fresh index.
    void UnregisterSampledImage(BindlessImageIndex index);

private:
    struct BindlessKey
    {
        uint32_t ImageId = 0;
        VkSampler Sampler = VK_NULL_HANDLE;

        bool operator==(const BindlessKey&) const = default;
    };

    struct BindlessKeyHash
    {
        size_t operator()(const BindlessKey& k) const noexcept
        {
            const auto a = static_cast<uint64_t>(k.ImageId);
            const auto b = reinterpret_cast<uint64_t>(k.Sampler);
            uint64_t h = a * 1099511628211ull;
            h ^= b + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            return static_cast<size_t>(h);
        }
    };

    struct PipelineLayoutEntry
    {
        std::vector<VkPushConstantRange> PushConstants;
        VkPipelineLayout Layout = VK_NULL_HANDLE;
    };

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    VulkanBufferService* Buffers = nullptr;
    VulkanImageService* Images = nullptr;
    bool Valid = false;

    VkDescriptorPool Pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkDescriptorSet Set = VK_NULL_HANDLE;

    std::vector<PipelineLayoutEntry> PipelineLayouts;

    std::unordered_map<BindlessKey, BindlessImageIndex, BindlessKeyHash> BindlessLookup;
    std::vector<uint32_t> BindlessFreeSlots;
    uint32_t BindlessNextSlot = 0;

    [[nodiscard]] bool CreatePoolAndLayout();
    [[nodiscard]] bool AllocateSet();

    void WriteBindlessSlot(uint32_t slot, ImageHandle image, VkSampler sampler);
};
