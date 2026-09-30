#include <graphics/vulkan/UiTextureResidency.h>

#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <algorithm>
#include <iterator>

bool UiTextureResidency::Setup(const RendererServices& services)
{
    if (services.Images == nullptr || services.Descriptors == nullptr || services.Samplers == nullptr)
        return false;
    Services = &services;
    // Clamp, not repeat: a glyph atlas sampled past its edge should not wrap
    // into a neighbouring glyph.
    Sampler = services.Samplers->GetLinearClamp();
    return true;
}

void UiTextureResidency::Teardown()
{
    // Everything, retiring or not: the device is idle by the time the renderer
    // tears down, so there is nothing left to wait for.
    if (Services != nullptr)
    {
        for (const auto& [id, texture] : Generated)
            Destroy(texture);
        for (const RetiredTexture& retired : Retiring)
            Destroy(retired.Texture);
    }
    Generated.clear();
    Retiring.clear();
    HostImages.clear();
    Pending = {};
    Services = nullptr;
}

void UiTextureResidency::Queue(const UiTextureChanges& changes)
{
    Pending.Uploads.insert(Pending.Uploads.end(), changes.Uploads.begin(), changes.Uploads.end());
    Pending.Released.insert(Pending.Released.end(), changes.Released.begin(), changes.Released.end());
}

UiTextureResidency::Applied UiTextureResidency::Prepare(const GpuFrameRetirement& clock)
{
    Applied applied;
    if (Services == nullptr)
        return applied;

    Logger* log = Services->Logging != nullptr
        ? &Services->Logging->GetLogger<UiTextureResidency>() : nullptr;

    for (const UiTextureUpload& upload : Pending.Uploads)
    {
        if (Generated.contains(upload.Id))
            continue;

        ImageCreateInfo info{};
        // UNORM, not SRGB: the runtime converted these once on the CPU and they
        // already hold linear premultiplied values. An sRGB view would decode
        // them a second time.
        info.Format = VK_FORMAT_R8G8B8A8_UNORM;
        info.Extent = VkExtent2D{ upload.Size.Width, upload.Size.Height };
        info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.DebugName = "ui_generated";

        const ImageHandle image = Services->Images->Create(info);
        if (!image.IsValid())
        {
            if (log != nullptr)
                log->Error("could not create a generated UI texture");
            continue;
        }
        if (!Services->Images->Upload(image, upload.LinearPremultipliedRgba8.data(),
                                      upload.LinearPremultipliedRgba8.size()))
        {
            Services->Images->Destroy(image);
            continue;
        }

        ++applied.Uploads;
        applied.UploadBytes += static_cast<std::uint64_t>(upload.Size.Width) * upload.Size.Height * 4u;
        Generated.emplace(upload.Id, GeneratedTexture{
            .Image = image,
            .Bindless = Services->Descriptors->RegisterSampledImage(image, Sampler),
        });
    }

    for (const UiGeneratedTextureId id : Pending.Released)
    {
        const auto it = Generated.find(id);
        if (it == Generated.end())
            continue;
        Retiring.push_back(RetiredTexture{ it->second, clock.Stamp() });
        Generated.erase(it);
    }
    Pending = {};

    const auto retired = std::remove_if(Retiring.begin(), Retiring.end(),
        [&](const RetiredTexture& entry) {
            if (!clock.IsRetired(entry.RetireStamp))
                return false;
            Destroy(entry.Texture);
            return true;
        });
    Retiring.erase(retired, Retiring.end());
    return applied;
}

std::optional<BindlessImageIndex> UiTextureResidency::FindGenerated(UiGeneratedTextureId id) const
{
    const auto it = Generated.find(id);
    if (it == Generated.end())
        return std::nullopt;
    return it->second.Bindless;
}

void UiTextureResidency::PublishHostImage(UiHostImageId id, BindlessImageIndex image)
{
    HostImages[id] = image;
}

void UiTextureResidency::WithdrawHostImage(UiHostImageId id)
{
    HostImages.erase(id);
}

std::optional<BindlessImageIndex> UiTextureResidency::FindHost(UiHostImageId id) const
{
    const auto it = HostImages.find(id);
    if (it == HostImages.end())
        return std::nullopt;
    return it->second;
}

void UiTextureResidency::Destroy(const GeneratedTexture& texture)
{
    Services->Descriptors->UnregisterSampledImage(texture.Bindless);
    Services->Images->Destroy(texture.Image);
}
