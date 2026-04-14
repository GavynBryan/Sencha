#include <assets/TextureCache.h>

#include <assets/ImageLoader.h>
#include <render/backend/vulkan/VulkanDescriptorCache.h>
#include <render/backend/vulkan/VulkanImageService.h>
#include <render/backend/vulkan/VulkanSamplerCache.h>

#include <cassert>
#include <cstring>

namespace
{
    constexpr uint32_t kIndexBits    = 20u;
    constexpr uint32_t kIndexMask    = (1u << kIndexBits) - 1u;
    constexpr uint32_t kMaxGeneration = (1u << (32u - kIndexBits)) - 1u;

    uint32_t DecodeIndex(uint32_t id)      { return id & kIndexMask; }
    uint32_t DecodeGeneration(uint32_t id) { return id >> kIndexBits; }

    VkFormat ToVkFormat(PixelFormat fmt)
    {
        switch (fmt)
        {
            case PixelFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case PixelFormat::RGBA8:      return VK_FORMAT_R8G8B8A8_UNORM;
        }
        return VK_FORMAT_R8G8B8A8_SRGB;
    }
}

TextureCache::TextureCache(LoggingProvider& logging,
                           VulkanImageService& images,
                           VulkanDescriptorCache& descriptors,
                           VulkanSamplerCache& samplers)
    : Log(logging.GetLogger<TextureCache>())
    , Images(&images)
    , Descriptors(&descriptors)
    , Samplers(&samplers)
{
    if (!images.IsValid() || !descriptors.IsValid())
    {
        Log.Error("Cannot create TextureCache: upstream services not valid");
        return;
    }

    Entries.emplace_back(); // reserve slot 0 so Id==0 stays invalid
    Valid = true;
}

TextureCache::~TextureCache()
{
    for (size_t i = 1; i < Entries.size(); ++i)
    {
        auto& entry = Entries[i];
        if (!entry.GpuImage.IsValid())
            continue;

        if (entry.Bindless.IsValid())
            Descriptors->UnregisterSampledImage(entry.Bindless);

        Images->Destroy(entry.GpuImage);
        entry.GpuImage = {};
    }
}

TextureHandle TextureCache::Acquire(std::string_view path, const SamplerDesc& sampler)
{
    const std::string key(path);

    if (auto it = PathLookup.find(key); it != PathLookup.end())
    {
        TextureHandle handle = it->second;
        if (auto* entry = Resolve(handle))
            ++entry->RefCount;
        return handle;
    }

    auto loadedImage = LoadImageFromFile(path);
    if (!loadedImage)
    {
        Log.Error("TextureCache: failed to load image '{}'", key);
        return {};
    }

    TextureHandle handle = UploadAndRegister(*loadedImage, sampler, key.c_str());
    if (handle.IsValid())
    {
        auto* entry = Resolve(handle);
        assert(entry);
        entry->PathKey = key;
        PathLookup.emplace(key, handle);
    }

    return handle;
}

TextureCacheHandle TextureCache::AcquireOwned(std::string_view path, const SamplerDesc& sampler)
{
    TextureHandle handle = Acquire(path, sampler);
    if (!handle.IsValid())
        return {};

    // TextureCacheHandle calls Attach on construction, which would double-count.
    // Use NoAttach: Acquire() already incremented the refcount above.
    return TextureCacheHandle(this, handle, TextureCacheHandle::NoAttach);
}

TextureHandle TextureCache::CreateFromImage(const Image& image,
                                             const SamplerDesc& sampler,
                                             const char* debugName)
{
    if (!image.IsValid())
    {
        Log.Error("TextureCache: CreateFromImage called with invalid Image");
        return {};
    }
    return UploadAndRegister(image, sampler, debugName);
}

BindlessImageIndex TextureCache::GetBindlessIndex(TextureHandle handle) const
{
    const auto* entry = Resolve(handle);
    if (!entry)
        return {};
    return entry->Bindless;
}

VkExtent2D TextureCache::GetExtent(TextureHandle handle) const
{
    const auto* entry = Resolve(handle);
    if (!entry)
        return {};
    return entry->Extent;
}

void TextureCache::Release(TextureHandle handle)
{
    auto* entry = Resolve(handle);
    if (!entry) return;

    assert(entry->RefCount > 0 && "TextureCache: Release called on entry with zero refcount");
    if (entry->RefCount == 0) return;

    --entry->RefCount;
    if (entry->RefCount > 0) return;

    const uint32_t index = DecodeIndex(handle.Id);
    FreeEntry(index, *entry);
}

// ILifetimeOwner — called by TextureCacheHandle construction and destruction.

void TextureCache::Attach(uint64_t token)
{
    TextureHandle handle{};
    std::memcpy(&handle, &token, sizeof(handle));
    if (auto* entry = Resolve(handle))
        ++entry->RefCount;
}

void TextureCache::Detach(uint64_t token)
{
    TextureHandle handle{};
    std::memcpy(&handle, &token, sizeof(handle));
    Release(handle);
}

// -- Private helpers --------------------------------------------------------

TextureHandle TextureCache::UploadAndRegister(const Image& image,
                                               const SamplerDesc& sampler,
                                               const char* debugName)
{
    assert(image.IsValid());

    ImageCreateInfo info;
    info.Format    = ToVkFormat(image.Format);
    info.Extent    = { image.Width, image.Height };
    info.Usage     = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.DebugName = debugName;

    ImageHandle gpuImage = Images->Create(info);
    if (!gpuImage.IsValid())
    {
        Log.Error("TextureCache: VulkanImageService::Create failed");
        return {};
    }

    if (!Images->Upload(gpuImage, image.Pixels.data(),
                        static_cast<VkDeviceSize>(image.ByteSize())))
    {
        Log.Error("TextureCache: VulkanImageService::Upload failed");
        Images->Destroy(gpuImage);
        return {};
    }

    VkSampler vkSampler = Samplers->Get(sampler);
    BindlessImageIndex bindless = Descriptors->RegisterSampledImage(gpuImage, vkSampler);
    if (!bindless.IsValid())
    {
        Log.Error("TextureCache: bindless descriptor slot exhausted");
        Images->Destroy(gpuImage);
        return {};
    }

    TextureEntry entry;
    entry.GpuImage = gpuImage;
    entry.Bindless = bindless;
    entry.Extent   = { image.Width, image.Height };

    return AllocHandle(std::move(entry));
}

TextureHandle TextureCache::AllocHandle(TextureEntry entry)
{
    uint32_t index = 0;
    entry.RefCount = 1;

    if (!FreeSlots.empty())
    {
        index = FreeSlots.back();
        FreeSlots.pop_back();

        uint32_t gen = Entries[index].Generation + 1u;
        if (gen == 0u || gen > kMaxGeneration) gen = 1u;
        entry.Generation = gen;
        Entries[index] = std::move(entry);
    }
    else
    {
        index = static_cast<uint32_t>(Entries.size());
        entry.Generation = 1u;
        Entries.emplace_back(std::move(entry));
    }

    return MakeHandle(index, Entries[index].Generation);
}

void TextureCache::FreeEntry(uint32_t index, TextureEntry& entry)
{
    if (!entry.PathKey.empty())
    {
        PathLookup.erase(entry.PathKey);
        entry.PathKey.clear();
    }

    if (entry.Bindless.IsValid())
    {
        Descriptors->UnregisterSampledImage(entry.Bindless);
        entry.Bindless = {};
    }

    if (entry.GpuImage.IsValid())
    {
        Images->Destroy(entry.GpuImage);
        entry.GpuImage = {};
    }

    entry.Extent = {};
    // Generation stays — bumped on reuse by AllocHandle.
    FreeSlots.push_back(index);
}

TextureCache::TextureEntry* TextureCache::Resolve(TextureHandle handle)
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen   = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    auto& entry = Entries[index];
    if (entry.Generation != gen || !entry.GpuImage.IsValid()) return nullptr;
    return &entry;
}

const TextureCache::TextureEntry* TextureCache::Resolve(TextureHandle handle) const
{
    if (!handle.IsValid()) return nullptr;
    const uint32_t index = DecodeIndex(handle.Id);
    const uint32_t gen   = DecodeGeneration(handle.Id);
    if (index == 0 || index >= Entries.size()) return nullptr;
    const auto& entry = Entries[index];
    if (entry.Generation != gen || !entry.GpuImage.IsValid()) return nullptr;
    return &entry;
}

TextureHandle TextureCache::MakeHandle(uint32_t index, uint32_t generation)
{
    TextureHandle h;
    h.Id = (generation << kIndexBits) | (index & kIndexMask);
    return h;
}
