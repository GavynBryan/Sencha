#include "ViewportTargetCache.h"

#include <graphics/vulkan/VulkanSamplerCache.h>

#include <algorithm>

namespace
{
constexpr VkFormat kColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
}

void ViewportTargetCache::Setup(const RendererServices& services)
{
    Services = services;
    Store.Setup(services);
    // Nearest, not linear: the target is displayed 1:1, and bilinear resampling
    // beats against the 1px grid and wireframe lines (moire). Nearest copies
    // pixels verbatim, so the composited image matches direct-to-swapchain
    // rendering.
    Presenter.Setup(services.Samplers->GetNearestClamp());
}

void ViewportTargetCache::Teardown()
{
    // Order matters: the presenter's sets reference views the store owns, and
    // the store waits the device out, which is also what makes freeing the
    // sets safe.
    for (Entry& entry : Entries)
        Release(entry);
    Entries.clear();
    Store.Teardown();
    Presenter.Teardown();
}

void ViewportTargetCache::BeginFrame(uint32_t frameInFlightIndex,
                                     GpuFrameRetirement retirement)
{
    Store.BeginFrame(frameInFlightIndex, retirement);
    Presenter.BeginFrame(retirement);
}

ViewportTargetCache::Entry* ViewportTargetCache::Find(ViewportId id)
{
    const auto it = std::find_if(Entries.begin(), Entries.end(),
                                 [id](const Entry& e) { return e.Id == id; });
    return it != Entries.end() ? &*it : nullptr;
}

ViewportTargetCache::Entry& ViewportTargetCache::FindOrAdd(ViewportId id)
{
    if (Entry* existing = Find(id))
        return *existing;

    RenderTargetDesc scene{};
    scene.ColorFormat = kColorFormat;
    scene.DepthFormat = Services.DepthFormat;
    // ImGui binds this one itself, through the presenter.
    scene.Read = RenderTargetRead::Sampled;
    scene.DebugName = "viewport_color";

    // Full resolution, not half: a half-res glow source stair-steps the
    // wireframe lines, and the blur cannot smooth diagonal stair-steps, so the
    // halo looks wavy. Full-res keeps the source line straight, at the cost of
    // a few extra full-res passes. A linear sampler gives a smooth blur.
    RenderTargetDesc bloom{};
    bloom.ColorFormat = kColorFormat;
    bloom.Read = RenderTargetRead::Bindless;
    bloom.Sampler = Services.Samplers->GetLinearClamp();
    bloom.DebugName = "viewport_bloom";

    Entries.push_back(Entry{
        .Id = id,
        .Scene = Store.Create(scene),
        .Bloom = { Store.Create(bloom), Store.Create(bloom) },
    });
    return Entries.back();
}

void ViewportTargetCache::Release(Entry& entry)
{
    Presenter.Release(entry.Scene);
    Store.Destroy(entry.Scene);
    for (RenderTargetId& bloom : entry.Bloom)
        Store.Destroy(bloom);
}

std::optional<ViewportTargetCache::RenderView> ViewportTargetCache::AcquireForRender(ViewportId id)
{
    Entry* entry = Find(id);
    if (entry == nullptr)
        return std::nullopt;

    const std::optional<RenderTargetView> scene = Store.Acquire(entry->Scene);
    if (!scene.has_value())
        return std::nullopt;
    const std::optional<RenderTargetView> bloom0 = Store.Acquire(entry->Bloom[0]);
    const std::optional<RenderTargetView> bloom1 = Store.Acquire(entry->Bloom[1]);
    // The scene target is what the viewport needs to render at all; bloom is an
    // effect that degrades to off, so a failed bloom plane is not fatal here.
    // EditorBloomPass checks its images before recording.
    const bool haveBloom = bloom0.has_value() && bloom1.has_value();

    RenderView view{};
    view.Scene = entry->Scene;
    view.ColorImage = scene->ColorImage;
    view.DepthImage = scene->DepthImage;
    view.ColorView = scene->ColorView;
    view.DepthView = scene->DepthView;
    view.Extent = scene->Extent;
    view.ColorLayout = scene->ColorLayout;
    if (haveBloom)
    {
        view.BloomImage[0] = bloom0->ColorImage;
        view.BloomImage[1] = bloom1->ColorImage;
        view.BloomView[0] = bloom0->ColorView;
        view.BloomView[1] = bloom1->ColorView;
        view.BloomLayout[0] = bloom0->ColorLayout;
        view.BloomLayout[1] = bloom1->ColorLayout;
        view.BloomExtent = bloom0->Extent;
        view.BloomBindless[0] = bloom0->BindlessIndex;
        view.BloomBindless[1] = bloom1->BindlessIndex;
    }
    return view;
}

void ViewportTargetCache::Prune(std::span<const ViewportId> live)
{
    for (auto it = Entries.begin(); it != Entries.end();)
    {
        if (std::find(live.begin(), live.end(), it->Id) != live.end())
        {
            ++it;
            continue;
        }
        Release(*it);
        it = Entries.erase(it);
    }
}

ImTextureID ViewportTargetCache::Display(ViewportId id, VkExtent2D extent)
{
    Entry& entry = FindOrAdd(id);
    Store.SetExtent(entry.Scene, extent);
    for (RenderTargetId bloom : entry.Bloom)
        Store.SetExtent(bloom, extent);
    return Presenter.Present(Store, entry.Scene);
}
