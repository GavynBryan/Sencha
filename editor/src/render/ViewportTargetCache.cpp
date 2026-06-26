#include "ViewportTargetCache.h"

#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanSamplerCache.h>

#include <imgui_impl_vulkan.h>

#include <algorithm>

namespace
{
constexpr VkFormat kColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
}

void ViewportTargetCache::Setup(const RendererServices& services)
{
    Services = services;
}

void ViewportTargetCache::Teardown()
{
    // Freeing descriptor sets requires the GPU to have finished with them, but the
    // engine may still have frames in flight when features tear down.
    if (Services.Device != nullptr)
        vkDeviceWaitIdle(Services.Device->GetDevice());
    for (Target& t : Targets)
        for (Slot& s : t.Slots)
            DestroySlot(s);
    Targets.clear();
    FlushRetiredSets(/*force*/ true);
}

void ViewportTargetCache::BeginFrame(uint32_t frameInFlightIndex)
{
    ++FrameCounter;
    CurrentSlot = frameInFlightIndex < kMaxSlots ? frameInFlightIndex : 0;
    FlushRetiredSets(/*force*/ false);
}

void ViewportTargetCache::FlushRetiredSets(bool force)
{
    for (auto it = RetiredSets.begin(); it != RetiredSets.end();)
    {
        if (force || it->RetireAfter <= FrameCounter)
        {
            ImGui_ImplVulkan_RemoveTexture(it->Set);
            it = RetiredSets.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

ViewportTargetCache::Target* ViewportTargetCache::Find(ViewportId id)
{
    const auto it = std::find_if(Targets.begin(), Targets.end(),
                                 [id](const Target& t) { return t.Id == id; });
    return it != Targets.end() ? &*it : nullptr;
}

ViewportTargetCache::Target& ViewportTargetCache::FindOrAdd(ViewportId id)
{
    if (Target* existing = Find(id))
        return *existing;
    Targets.push_back(Target{ .Id = id });
    return Targets.back();
}

void ViewportTargetCache::DestroySlot(Slot& slot)
{
    if (slot.ImGuiSet != VK_NULL_HANDLE)
    {
        // Defer: a prior in-flight frame may still sample this set.
        RetiredSets.push_back({ slot.ImGuiSet, FrameCounter + kRetireFrames });
        slot.ImGuiSet = VK_NULL_HANDLE;
    }
    if (!slot.Color.IsNull())
    {
        Services.Images->Destroy(slot.Color);
        slot.Color = {};
    }
    if (!slot.Depth.IsNull())
    {
        Services.Images->Destroy(slot.Depth);
        slot.Depth = {};
    }
    for (int i = 0; i < 2; ++i)
    {
        if (slot.BloomBindless[i].IsValid())
        {
            Services.Descriptors->UnregisterSampledImage(slot.BloomBindless[i]);
            slot.BloomBindless[i] = {};
        }
        if (!slot.Bloom[i].IsNull())
        {
            Services.Images->Destroy(slot.Bloom[i]);
            slot.Bloom[i] = {};
        }
        slot.BloomLayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    slot.Extent = {};
    slot.BloomExtent = {};
    slot.ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void ViewportTargetCache::ResizeSlot(Slot& slot, VkExtent2D extent)
{
    // Destroy defers through the image service's deletion queue, so retiring the old
    // images here is safe even while a prior frame still samples them.
    DestroySlot(slot);

    ImageCreateInfo color;
    color.Format = kColorFormat;
    color.Extent = extent;
    color.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    color.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    color.DebugName = "viewport_color";
    slot.Color = Services.Images->Create(color);

    ImageCreateInfo depth;
    depth.Format = Services.DepthFormat;
    depth.Extent = extent;
    depth.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth.AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth.DebugName = "viewport_depth";
    slot.Depth = Services.Images->Create(depth);

    if (slot.Color.IsNull() || slot.Depth.IsNull())
    {
        DestroySlot(slot);
        return;
    }

    slot.Extent = extent;
    slot.ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Nearest, not linear: the target is displayed 1:1, and bilinear resampling beats
    // against the 1px grid/wireframe lines (moire). Nearest copies pixels verbatim, so
    // the composited image matches direct-to-swapchain rendering.
    slot.ImGuiSet = ImGui_ImplVulkan_AddTexture(
        Services.Samplers->GetNearestClamp(),
        Services.Images->GetView(slot.Color),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Bloom ping-pong targets + bindless sample slots; a linear sampler gives a smooth
    // blur. Full resolution (not half): a half-res glow source stair-steps the wireframe
    // lines, and the blur can't smooth diagonal stair-steps, so the halo looks wavy.
    // Full-res keeps the source line straight. (Cost is a few extra full-res passes.)
    slot.BloomExtent = extent;
    for (int i = 0; i < 2; ++i)
    {
        ImageCreateInfo bloom;
        bloom.Format = kColorFormat;
        bloom.Extent = slot.BloomExtent;
        bloom.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        bloom.AspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bloom.DebugName = "viewport_bloom";
        slot.Bloom[i] = Services.Images->Create(bloom);
        slot.BloomLayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    const VkSampler bloomSampler = Services.Samplers->GetLinearClamp();
    slot.BloomBindless[0] = Services.Descriptors->RegisterSampledImage(slot.Bloom[0], bloomSampler);
    slot.BloomBindless[1] = Services.Descriptors->RegisterSampledImage(slot.Bloom[1], bloomSampler);
}

std::optional<ViewportTargetCache::RenderView> ViewportTargetCache::AcquireForRender(ViewportId id)
{
    Target* t = Find(id);
    if (t == nullptr || t->Requested.width == 0 || t->Requested.height == 0)
        return std::nullopt;

    Slot& slot = t->Slots[CurrentSlot];
    if (slot.Color.IsNull() || slot.Extent.width != t->Requested.width
        || slot.Extent.height != t->Requested.height)
        ResizeSlot(slot, t->Requested);
    if (slot.Color.IsNull())
        return std::nullopt;

    return RenderView{
        .ColorImage = Services.Images->GetImage(slot.Color),
        .DepthImage = Services.Images->GetImage(slot.Depth),
        .ColorView = Services.Images->GetView(slot.Color),
        .DepthView = Services.Images->GetView(slot.Depth),
        .Extent = slot.Extent,
        .ColorLayout = &slot.ColorLayout,
        .BloomImage = { Services.Images->GetImage(slot.Bloom[0]), Services.Images->GetImage(slot.Bloom[1]) },
        .BloomView = { Services.Images->GetView(slot.Bloom[0]), Services.Images->GetView(slot.Bloom[1]) },
        .BloomLayout = { &slot.BloomLayout[0], &slot.BloomLayout[1] },
        .BloomExtent = slot.BloomExtent,
        .BloomBindless = { slot.BloomBindless[0].Value, slot.BloomBindless[1].Value },
    };
}

void ViewportTargetCache::Prune(std::span<const ViewportId> live)
{
    for (auto it = Targets.begin(); it != Targets.end();)
    {
        const bool keep = std::find(live.begin(), live.end(), it->Id) != live.end();
        if (keep)
        {
            ++it;
            continue;
        }
        for (Slot& s : it->Slots)
            DestroySlot(s);
        it = Targets.erase(it);
    }
}

ImTextureID ViewportTargetCache::Display(ViewportId id, VkExtent2D extent)
{
    Target& t = FindOrAdd(id);
    t.Requested = extent;
    const VkDescriptorSet set = t.Slots[CurrentSlot].ImGuiSet;
    // VkDescriptorSet -> ImTextureID (ImU64), the ImGui Vulkan backend's convention.
    return set != VK_NULL_HANDLE ? (ImTextureID)set : (ImTextureID)0;
}
