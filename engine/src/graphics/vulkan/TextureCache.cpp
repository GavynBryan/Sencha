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

    VkFormat ToVkFormat(TexturePixelFormat fmt)
    {
        switch (fmt)
        {
            case TexturePixelFormat::RGBA8:      return VK_FORMAT_R8G8B8A8_UNORM;
            case TexturePixelFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case TexturePixelFormat::BC4:        return VK_FORMAT_BC4_UNORM_BLOCK;
            case TexturePixelFormat::BC5:        return VK_FORMAT_BC5_UNORM_BLOCK;
            case TexturePixelFormat::BC7:        return VK_FORMAT_BC7_UNORM_BLOCK;
            case TexturePixelFormat::BC7_SRGB:   return VK_FORMAT_BC7_SRGB_BLOCK;
            default:                             return VK_FORMAT_UNDEFINED;
        }
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

TextureHandle TextureCache::CreateFromImage(std::string_view name,
                                             const Image& image,
                                             const SamplerDesc& sampler)
{
    if (name.empty())
        return CreateFromImage(image, sampler);

    if (TextureHandle existing = FindRegisteredHandle(name, /*addRef*/ true); existing.IsValid())
        return existing;

    if (!image.IsValid())
    {
        Log.Error("TextureCache: CreateFromImage called with invalid Image for '{}'", name);
        return {};
    }

    TextureEntry entry;
    if (!UploadImage(image, sampler, std::string(name).c_str(), entry))
        return {};

    return AllocNamedHandle(name, std::move(entry));
}

TextureHandle TextureCache::CreateFromTextureData(std::string_view name,
                                                  const TextureData& texture,
                                                  const SamplerDesc& sampler)
{
    if (TextureHandle existing = FindRegisteredHandle(name, /*addRef*/ true); existing.IsValid())
        return existing;

    if (!ValidateTextureData(texture))
    {
        Log.Error("TextureCache: structurally invalid TextureData for '{}'", name);
        return {};
    }

    const VkFormat format = ToVkFormat(texture.Format);
    if (format == VK_FORMAT_UNDEFINED)
    {
        Log.Error("TextureCache: unsupported cooked format {} for '{}'",
                  static_cast<uint32_t>(texture.Format), name);
        return {};
    }

    ImageCreateInfo info;
    info.Format = format;
    info.Extent = { texture.Width, texture.Height };
    info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.MipLevels = static_cast<uint32_t>(texture.Mips.size());
    info.GenerateMips = false;
    const std::string debugName(name);
    info.DebugName = debugName.c_str();

    ImageHandle gpuImage = Images->Create(info);
    if (!gpuImage.IsValid())
    {
        Log.Error("TextureCache: VulkanImageService::Create failed for '{}'", name);
        return {};
    }

    std::vector<VulkanImageService::MipUploadRegion> regions;
    regions.reserve(texture.Mips.size());
    for (uint32_t i = 0; i < texture.Mips.size(); ++i)
    {
        const TextureMipLevel& mip = texture.Mips[i];
        regions.push_back(VulkanImageService::MipUploadRegion{
            .MipLevel = i,
            .Width = mip.Width,
            .Height = mip.Height,
            .Offset = static_cast<VkDeviceSize>(mip.Offset),
        });
    }

    if (!Images->UploadMips(gpuImage, texture.Blob.data(),
                            static_cast<VkDeviceSize>(texture.Blob.size()), regions))
    {
        Log.Error("TextureCache: mip-chain upload failed for '{}'", name);
        Images->Destroy(gpuImage);
        return {};
    }

    VkSampler vkSampler = Samplers->Get(sampler);
    BindlessImageIndex bindless = Descriptors->RegisterSampledImage(gpuImage, vkSampler);
    if (!bindless.IsValid())
    {
        Log.Error("TextureCache: bindless descriptor slot exhausted for '{}'", name);
        Images->Destroy(gpuImage);
        return {};
    }

    TextureEntry entry;
    entry.GpuImage = gpuImage;
    entry.Bindless = bindless;
    entry.Extent = { texture.Width, texture.Height };
    return name.empty() ? AllocHandle(std::move(entry)) : AllocNamedHandle(name, std::move(entry));
}

TextureHandle TextureCache::Find(std::string_view name) const
{
    return FindRegisteredHandle(name);
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
