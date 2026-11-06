#pragma once

#include "render/DisplayLedger.h"
#include "viewport/ViewportId.h"

#include <graphics/GpuFrameRetirement.h>
#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/RenderTargetStore.h>
#include <graphics/vulkan/Renderer.h>


#include <cstdint>
#include <span>
#include <vector>

// Maps kyusu's viewports onto engine render targets: one scene target (HDR
// colour + depth) plus two full-resolution bloom ping-pong planes per viewport.
// The scene target is bindless, so the authored UI draws it as a host image.
//
// Kyusu-local, not editor-common: the bloom pair per viewport is this editor's
// policy, and its one other consumer -- the material preview -- owns a
// single target directly rather than inventing a viewport identity for it.
//
// The images, their per-frame-in-flight slots, resizing, and retirement all
// belong to RenderTargetStore; what is left here is which ViewportId owns
// which targets, the bloom pair being a property of an editor viewport rather
// than of targets in general.
//
// The render side (Offscreen phase) calls BeginFrame then AcquireForRender per
// viewport; the arrangement calls Show with each shown view's size.
class ViewportTargetCache
{
public:
    void Setup(const RendererServices& services);
    void Teardown();

    struct RenderView
    {
        // The display target these images belong to, for a caller that has to
        // name the target rather than attach it -- a declared frame view says
        // where it renders before anything resolves the images.
        RenderTargetId Display;
        // What the UI samples: the scene through the output pass, then chrome.
        VkImage       ColorImage = VK_NULL_HANDLE;
        VkImage       DepthImage = VK_NULL_HANDLE;
        VkImageView   ColorView = VK_NULL_HANDLE;
        VkImageView   DepthView = VK_NULL_HANDLE;
        VkExtent2D    Extent{};
        VkImageLayout* ColorLayout = nullptr; // caller updates after its transitions
        // The display colour's bindless index, readable once its layout is
        // SHADER_READ_ONLY_OPTIMAL.
        uint32_t      ColorBindless = UINT32_MAX;
        // Scene-referred light, consumed by the output pass within the frame.
        VkImage        SceneImage = VK_NULL_HANDLE;
        VkImageView    SceneView = VK_NULL_HANDLE;
        RenderTargetAccess* SceneAccess = nullptr;
        uint32_t       SceneBindless = UINT32_MAX;
        // Bloom scratch: two full-res ping-pong targets + the bindless sample indices.
        VkImage        BloomImage[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkImageView    BloomView[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkImageLayout* BloomLayout[2]{ nullptr, nullptr };
        VkExtent2D     BloomExtent{};
        uint32_t       BloomBindless[2]{ UINT32_MAX, UINT32_MAX };
    };

    // -- Render side (Offscreen phase), in this order ------------------------
    void BeginFrame(uint32_t frameInFlightIndex);
    // The current slot's images for `id`, sized to what the panel last
    // requested. nullopt until the panel has requested a size.
    [[nodiscard]] std::optional<RenderView> AcquireForRender(ViewportId id);
    // The view's outline mask, built on first use.
    [[nodiscard]] std::optional<RenderTargetView> AcquireOutlineMask(ViewportId id);
    // Releases the view's mask while nothing is outlined.
    void ReleaseOutlineMask(ViewportId id);
    // Destroy targets for viewports not in `live` (closed or re-laid-out).
    void Prune(std::span<const ViewportId> live);
    // Whether `id` was shown last frame; a viewport nobody shows does not
    // render.
    [[nodiscard]] bool WasDisplayed(ViewportId id) const { return Displays.WasDisplayed(id); }

    // -- Arrangement side ----------------------------------------------------
    // `id` is on screen this frame at `extent` pixels.
    void Show(ViewportId id, VkExtent2D extent);

private:
    struct Entry
    {
        ViewportId Id{};
        RenderTargetId Display;
        RenderTargetId Scene;
        RenderTargetId OutlineMask;
        RenderTargetId Bloom[2];
    };

    [[nodiscard]] Entry* Find(ViewportId id);
    [[nodiscard]] Entry& FindOrAdd(ViewportId id);
    void Release(Entry& entry);

    RendererServices Services{};
    RenderTargetStore Store;
    DisplayLedger<ViewportId> Displays;
    std::vector<Entry> Entries;
};
