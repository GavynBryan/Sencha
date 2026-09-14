#include "ui/ThemeTextureCache.h"

#include <assets/texture/Image.h>
#include <assets/texture/ImageLoader.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <system_error>
#include <utility>

namespace
{
// A resident source is re-examined a couple of times a second, not every
// frame: the point is to notice a save, and two stat calls per themed asset
// per frame would be work for nothing.
constexpr std::uint64_t kRestatInterval = 30;
}

ThemeTextureCache::ThemeTextureCache(VulkanImageService& images, VulkanSamplerCache& samplers, Logger* log)
    : Images(images)
    , Samplers(samplers)
    , Log(log)
{
}

ThemeTextureCache::~ThemeTextureCache()
{
    Shutdown();
}

void ThemeTextureCache::BeginFrame(GpuFrameRetirement retirement)
{
    Retirement = retirement;
    ++Frame;
    FlushRetired(/*force*/ false);
}

ThemeTextureCache::SourceStamp ThemeTextureCache::StampOf(const std::string& path)
{
    SourceStamp stamp;
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec)
        return stamp; // absent is a stamp of its own: creating the file changes it
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
        return stamp;
    stamp.Mtime = mtime;
    stamp.Size = size;
    stamp.Exists = true;
    return stamp;
}

bool ThemeTextureCache::ShouldRestat(const Entry& entry) const
{
    return entry.LastRequested == 0 || Frame % kRestatInterval == 0;
}

bool ThemeTextureCache::Load(Entry& entry)
{
    // Authored sRGB, uploaded to an sRGB format: the sampler decodes to linear,
    // which is the space the palette is stored in and the space the swapchain
    // encodes on write. An authored texel lands on screen as itself.
    const std::optional<Image> image = LoadImageFromFile(entry.Path, /*srgb*/ true);
    if (!image.has_value() || !image->IsValid())
    {
        if (Log != nullptr)
            Log->Warn("ThemeTextureCache: cannot load theme texture '{}'; the surface falls back to solid",
                      entry.Path);
        ++Stat.Failures;
        return false;
    }

    ImageCreateInfo info;
    info.Format = VK_FORMAT_R8G8B8A8_SRGB;
    info.Extent = { image->Width, image->Height };
    info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.DebugName = "theme texture";

    const ImageHandle uploaded = Images.Create(info);
    if (!uploaded.IsValid() || !Images.Upload(uploaded, image->Pixels.data(),
                                              static_cast<VkDeviceSize>(image->ByteSize())))
    {
        if (uploaded.IsValid())
            Images.Destroy(uploaded);
        if (Log != nullptr)
            Log->Warn("ThemeTextureCache: cannot upload theme texture '{}'", entry.Path);
        ++Stat.Failures;
        return false;
    }

    // Repeat, not clamp: a strip narrower than the surface it dresses is tiled
    // by drawing one quad with a UV above one, which only works if the sampler
    // wraps.
    entry.Set = ImGui_ImplVulkan_AddTexture(Samplers.GetLinearRepeat(), Images.GetView(uploaded),
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (entry.Set == VK_NULL_HANDLE)
    {
        Images.Destroy(uploaded);
        ++Stat.Failures;
        return false;
    }
    entry.Image = uploaded;
    entry.Size = ImVec2(static_cast<float>(image->Width), static_cast<float>(image->Height));
    ++Stat.Loads;
    // Logged, not silent: an unattended run's log is how a theme switch is
    // shown to have loaded one texture and rebuilt no fonts.
    if (Log != nullptr)
        Log->Info("ThemeTextureCache: loaded '{}' ({}x{})", entry.Path, image->Width, image->Height);
    return true;
}

void ThemeTextureCache::ReleaseResources(Entry& entry, bool immediate)
{
    if (entry.Set != VK_NULL_HANDLE)
    {
        if (immediate)
            ImGui_ImplVulkan_RemoveTexture(entry.Set);
        else
            Retire(entry.Set);
        entry.Set = VK_NULL_HANDLE;
    }
    if (entry.Image.IsValid())
    {
        // Already deferred: VulkanImageService holds the destroy behind the
        // deletion queue for frames-in-flight, so nothing in flight can still
        // be sampling it.
        Images.Destroy(entry.Image);
        entry.Image = {};
    }
    entry.Size = ImVec2(0.0f, 0.0f);
}

void ThemeTextureCache::Retire(VkDescriptorSet set)
{
    if (set != VK_NULL_HANDLE)
        Retired.push_back(RetiredSet{ set, Retirement.Stamp() });
}

void ThemeTextureCache::FlushRetired(bool force)
{
    for (auto it = Retired.begin(); it != Retired.end();)
    {
        if (force || Retirement.IsRetired(it->Stamp))
        {
            ImGui_ImplVulkan_RemoveTexture(it->Set);
            it = Retired.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void ThemeTextureCache::Prepare(std::span<const std::string> paths)
{
    for (const std::string& path : paths)
    {
        if (path.empty())
            continue;
        auto it = std::find_if(Entries.begin(), Entries.end(),
                               [&path](const Entry& e) { return e.Path == path; });
        if (it == Entries.end())
        {
            Entry fresh;
            fresh.Path = path;
            Entries.push_back(std::move(fresh));
            it = Entries.end() - 1;
        }

        Entry& entry = *it;
        if (ShouldRestat(entry))
        {
            const SourceStamp stamp = StampOf(path);
            const bool first = entry.LastRequested == 0;
            if (first || stamp != entry.Stamp)
            {
                // A changed (or newly appeared, or repaired) source replaces
                // whatever was resident under the same path.
                const bool had = entry.Set != VK_NULL_HANDLE;
                ReleaseResources(entry, /*immediate*/ false);
                entry.Stamp = stamp;
                entry.Failed = !Load(entry);
                if (had)
                    ++Stat.Replacements;
            }
        }
        entry.LastRequested = Frame;
    }

    // Anything the current style state no longer asks for.
    for (auto it = Entries.begin(); it != Entries.end();)
    {
        if (it->LastRequested == Frame)
        {
            ++it;
            continue;
        }
        ReleaseResources(*it, /*immediate*/ false);
        it = Entries.erase(it);
    }
    Stat.Resident = Entries.size();
}

ThemeTextureCache::Texture ThemeTextureCache::Get(std::string_view path) const
{
    const auto it = std::find_if(Entries.begin(), Entries.end(),
                                 [path](const Entry& e) { return e.Path == path; });
    if (it == Entries.end() || it->Set == VK_NULL_HANDLE)
        return {};
    // VkDescriptorSet -> ImTextureID (ImU64), the Vulkan backend's convention.
    return Texture{ (ImTextureID)it->Set, it->Size };
}

void ThemeTextureCache::Shutdown()
{
    for (Entry& entry : Entries)
        ReleaseResources(entry, /*immediate*/ true);
    Entries.clear();
    FlushRetired(/*force*/ true);
    Stat.Resident = 0;
}

ThemeTextureCache::Counters ThemeTextureCache::Stats() const
{
    return Stat;
}
