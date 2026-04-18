#include <graphics/vulkan/TextureCache.h>

#include <render/ImageLoader.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <cassert>

namespace
{
    VkFormat ToVkFormat(PixelFormat fmt)
    {
        switch (fmt)
        {
            case PixelFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case PixelFormat::RGBA8:      return VK_FORMAT_R8G8B8A8_UNORM;
        }
        return VK_FORMAT_R8G8B8A8_SRGB;
    }
} // namespace

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

    ReserveNullSlot();
    Valid = true;
}

TextureCache::~TextureCache()
{
    FreeAllEntries();
}

// -- Acquire overloads (SamplerDesc is texture-specific) ---------------------

TextureHandle TextureCache::Acquire(std::string_view path, const SamplerDesc& sampler)
{
    PendingSampler = &sampler;
    TextureHandle handle = AssetCache::Acquire(path);
    PendingSampler = nullptr;
    return handle;
}

TextureCacheHandle TextureCache::AcquireOwned(std::string_view path, const SamplerDesc& sampler)
{
    PendingSampler = &sampler;
    TextureCacheHandle handle = AssetCache::AcquireOwned(path);
    PendingSampler = nullptr;
    return handle;
}

// -- CreateFromImage ----------------------------------------------------------

TextureHandle TextureCache::CreateFromImage(const Image& image,
                                             const SamplerDesc& sampler,
                                             const char* debugName)
{
    if (!image.IsValid())
    {
        Log.Error("TextureCache: CreateFromImage called with invalid Image");
        return {};
    }

    TextureEntry entry;
    if (!UploadImage(image, sampler, debugName, entry))
        return {};

    return AllocHandle(std::move(entry));
}

// -- Accessors ----------------------------------------------------------------

BindlessImageIndex TextureCache::GetBindlessIndex(TextureHandle handle) const
{
    const TextureEntry* entry = Resolve(handle);
    return entry ? entry->Bindless : BindlessImageIndex{};
}

VkExtent2D TextureCache::GetExtent(TextureHandle handle) const
{
    const TextureEntry* entry = Resolve(handle);
    return entry ? entry->Extent : VkExtent2D{};
}

// -- AssetCache CRTP hooks ----------------------------------------------------

bool TextureCache::OnLoad(std::string_view path, TextureEntry& out)
{
    auto loadedImage = LoadImageFromFile(path);
    if (!loadedImage)
    {
        Log.Error("TextureCache: failed to load image '{}'", path);
        return false;
    }

    const SamplerDesc sampler = PendingSampler ? *PendingSampler : SamplerDesc{};
    return UploadImage(*loadedImage, sampler, std::string(path).c_str(), out);
}

void TextureCache::OnFree(TextureEntry& entry)
{
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
}

bool TextureCache::IsEntryLive(const TextureEntry& entry) const
{
    return entry.GpuImage.IsValid();
}

// -- Private ------------------------------------------------------------------

bool TextureCache::UploadImage(const Image& image,
                                const SamplerDesc& sampler,
                                const char* debugName,
                                TextureEntry& out)
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
        return false;
    }

    if (!Images->Upload(gpuImage, image.Pixels.data(),
                        static_cast<VkDeviceSize>(image.ByteSize())))
    {
        Log.Error("TextureCache: VulkanImageService::Upload failed");
        Images->Destroy(gpuImage);
        return false;
    }

    VkSampler vkSampler = Samplers->Get(sampler);
    BindlessImageIndex bindless = Descriptors->RegisterSampledImage(gpuImage, vkSampler);
    if (!bindless.IsValid())
    {
        Log.Error("TextureCache: bindless descriptor slot exhausted");
        Images->Destroy(gpuImage);
        return false;
    }

    out.GpuImage = gpuImage;
    out.Bindless = bindless;
    out.Extent   = { image.Width, image.Height };
    return true;
}
