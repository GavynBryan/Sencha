#include "ViewportTargetCache.h"

#include <graphics/vulkan/OutlineMaskTargets.h>
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
}

void ViewportTargetCache::Teardown()
{
    // Order matters: the presenter's sets reference views the store owns, and
    // the store waits the device out, which is also what makes freeing the
    // sets safe.
    for (Entry& entry : Entries)
        Release(entry);
    Entries.clear();
    Displays = {};
    Store.Teardown();
}

void ViewportTargetCache::BeginFrame(uint32_t frameInFlightIndex)
{
    Store.BeginFrame(frameInFlightIndex);
    Displays.Advance();
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

    RenderTargetDesc display{};
    display.ColorFormat = kColorFormat;
    // Depth only, deliberately not the swapchain's format. That one carries a
    // stencil aspect for authored UI's clip mask, and a viewport target has no
    // use for one -- inheriting it would make every barrier on these images a
    // combined-aspect barrier for a feature they do not have.
    display.DepthFormat = VK_FORMAT_D32_SFLOAT;
    // Nearest, not linear: the target is shown 1:1, and bilinear resampling
    // beats against the 1px grid and wireframe lines (moire).
    display.Read = RenderTargetRead::Bindless;
    display.Sampler = Services.Samplers->GetNearestClamp();
    display.DebugName = "viewport_color";

    RenderTargetDesc scene{};
    scene.ColorFormat = kSceneColorFormat;
    scene.Read = RenderTargetRead::Bindless;
    scene.Sampler = Services.Samplers->GetNearestClamp();
    scene.DebugName = "viewport_scene";
    scene.Lifetime = RenderTargetLifetime::WithinFrame;

    RenderTargetDesc mask{};
    mask.ColorFormat = kOutlineMaskFormat;
    mask.Read = RenderTargetRead::Bindless;
    mask.Sampler = Services.Samplers->GetNearestClamp();
    mask.DebugName = "viewport_outline_mask";
    mask.Lifetime = RenderTargetLifetime::WithinFrame;
    RenderTargetDesc tiles = mask;
    tiles.ColorFormat = kOutlineTileFormat;
    tiles.DebugName = "viewport_outline_tiles";

    // Full resolution, not half: a half-res glow source stair-steps the
    // wireframe lines, and the blur cannot smooth diagonal stair-steps, so the
    // halo looks wavy. Full-res keeps the source line straight, at the cost of
    // a few extra full-res passes. A linear sampler gives a smooth blur.
    RenderTargetDesc bloom{};
    bloom.ColorFormat = kColorFormat;
    bloom.Read = RenderTargetRead::Bindless;
    bloom.Sampler = Services.Samplers->GetLinearClamp();
    bloom.DebugName = "viewport_bloom";
    // Written and read within one frame, so one image serves every frame in flight.
    bloom.Lifetime = RenderTargetLifetime::WithinFrame;

    Entries.push_back(Entry{
        .Id = id,
        .Display = Store.Create(display),
        .Scene = Store.Create(scene),
        .OutlineMask = Store.Create(mask),
        .OutlineTiles = { Store.Create(tiles), Store.Create(tiles) },
        .OutlineQuiet = {},
        .Bloom = { Store.Create(bloom), Store.Create(bloom) },
    });
    return Entries.back();
}

void ViewportTargetCache::Release(Entry& entry)
{
    Store.Destroy(entry.Display);
    Store.Destroy(entry.Scene);
    Store.Destroy(entry.OutlineMask);
    for (RenderTargetId tiles : entry.OutlineTiles)
        Store.Destroy(tiles);
    for (RenderTargetId& bloom : entry.Bloom)
        Store.Destroy(bloom);
}

std::optional<ViewportTargetCache::RenderView> ViewportTargetCache::AcquireForRender(ViewportId id)
{
    Entry* entry = Find(id);
    if (entry == nullptr)
        return std::nullopt;

    const std::optional<RenderTargetView> display = Store.Acquire(entry->Display);
    const std::optional<RenderTargetView> scene = Store.Acquire(entry->Scene);
    if (!display.has_value() || !scene.has_value())
        return std::nullopt;
    const std::optional<RenderTargetView> bloom0 = Store.Acquire(entry->Bloom[0]);
    const std::optional<RenderTargetView> bloom1 = Store.Acquire(entry->Bloom[1]);
    // These two are what the viewport needs to render at all; bloom is an
    // effect that degrades to off, so a failed bloom plane is not fatal here.
    // EditorBloomPass checks its images before recording.
    const bool haveBloom = bloom0.has_value() && bloom1.has_value();

    RenderView view{};
    view.Display = entry->Display;
    view.ColorImage = display->ColorImage;
    view.DepthImage = display->DepthImage;
    view.ColorView = display->ColorView;
    view.DepthView = display->DepthView;
    view.Extent = display->Extent;
    view.ColorLayout = display->ColorLayout;
    view.ColorBindless = display->BindlessIndex;
    view.SceneImage = scene->ColorImage;
    view.SceneView = scene->ColorView;
    view.SceneAccess = scene->ColorAccess;
    view.SceneBindless = scene->BindlessIndex;
    if (haveBloom)
    {
        view.BloomImage[0] = bloom0->ColorImage;
        view.BloomImage[1] = bloom1->ColorImage;
        view.BloomView[0] = bloom0->ColorView;
        view.BloomView[1] = bloom1->ColorView;
        view.BloomAccess[0] = bloom0->ColorAccess;
        view.BloomAccess[1] = bloom1->ColorAccess;
        view.BloomExtent = bloom0->Extent;
        view.BloomBindless[0] = bloom0->BindlessIndex;
        view.BloomBindless[1] = bloom1->BindlessIndex;
    }
    return view;
}

std::optional<ViewportTargetCache::OutlineViews> ViewportTargetCache::AcquireOutlineMask(ViewportId id)
{
    Entry* entry = Find(id);
    if (entry == nullptr)
        return std::nullopt;
    entry->OutlineQuiet.NoteUsed();
    const std::optional<RenderTargetView> mask = Store.Acquire(entry->OutlineMask);
    const std::optional<RenderTargetView> marked = Store.Acquire(entry->OutlineTiles[0]);
    const std::optional<RenderTargetView> near = Store.Acquire(entry->OutlineTiles[1]);
    if (!mask.has_value() || !marked.has_value() || !near.has_value())
        return std::nullopt;
    return OutlineViews{ .Mask = *mask, .Tiles = { .Marked = *marked, .Near = *near } };
}

void ViewportTargetCache::NoteOutlineMaskQuiet(ViewportId id)
{
    if (Entry* entry = Find(id); entry != nullptr && entry->OutlineQuiet.NoteQuiet())
    {
        Store.Evict(entry->OutlineMask);
        for (RenderTargetId tiles : entry->OutlineTiles)
            Store.Evict(tiles);
    }
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
        Displays.Forget(it->Id);
        Release(*it);
        it = Entries.erase(it);
    }
}

void ViewportTargetCache::Show(ViewportId id, VkExtent2D extent)
{
    Displays.MarkDisplayed(id);
    Entry& entry = FindOrAdd(id);
    Store.SetExtent(entry.Display, extent);
    Store.SetExtent(entry.Scene, extent);
    Store.SetExtent(entry.OutlineMask, extent);
    for (RenderTargetId tiles : entry.OutlineTiles)
        Store.SetExtent(tiles, OutlineTileExtent(extent));
    for (RenderTargetId bloom : entry.Bloom)
        Store.SetExtent(bloom, extent);
}
