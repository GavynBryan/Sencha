#pragma once

#include <graphics/FramesInFlight.h>
#include <graphics/GpuFrameRetirement.h>
#include <graphics/RenderTargetId.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanImageService.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

//=============================================================================
// RenderTargetStore
//
// Offscreen colour (+ depth) targets, sized on request, one set per frame in
// flight, destroyed through the fence-anchored retirement clock.
//
// This is the editor's ViewportTargetCache generalised and moved down. That
// cache is already shared by Kyusu's viewports and Shudei's material preview,
// but it lives in editor/common, is keyed by a ViewportId, hardcodes one
// descriptor, and knows about ImGui -- so the engine has no offscreen-target
// story at all, and a game-side second view has nowhere to render. Split
// screen, weapon scopes, render-to-texture, and cubemap capture all need one.
//
// Targets are per frame in flight because frames overlap on the GPU: a frame
// still sampling slot N's image must not be racing frame N+1's render into it.
// The slot comes from the engine's frames-in-flight authority rather than a
// local guess -- a private bound smaller than the configured count used to fold
// every frame onto slot 0, so the UI sampled the target the next frame was
// rendering into.
//
// What stays with the caller: image layout tracking (the store hands out a
// pointer to the layout it remembers, and the caller updates it after its own
// transitions, because barrier granularity is pass policy), and any binding a
// specific reader needs -- ImGui descriptor sets in particular, which is an
// editor concern and must not reach the engine.
//=============================================================================

// How a target will be read once it has been rendered into. This is the axis
// that decides image usage and whether the store maintains a bindless index.
enum class RenderTargetRead : std::uint8_t
{
    // Attachment only; nothing samples it.
    None,
    // Sampled, but the reader binds it itself. ImGui-displayed viewport
    // targets are this: the editor owns the descriptor set.
    Sampled,
    // Sampled through the bindless set, with the index maintained here.
    Bindless,
};

struct RenderTargetDesc
{
    VkExtent2D Extent{};
    VkFormat ColorFormat = VK_FORMAT_UNDEFINED;
    // VK_FORMAT_UNDEFINED for a colour-only target.
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    RenderTargetRead Read = RenderTargetRead::None;
    // Sampler for the bindless registration; ignored otherwise.
    VkSampler Sampler = VK_NULL_HANDLE;
    const char* DebugName = nullptr;

    [[nodiscard]] friend bool operator==(const RenderTargetDesc&,
                                         const RenderTargetDesc&) = default;
};

// One frame's worth of a target: what to attach, and where the store is
// remembering its layout.
struct RenderTargetView
{
    VkImage ColorImage = VK_NULL_HANDLE;
    VkImageView ColorView = VK_NULL_HANDLE;
    VkImage DepthImage = VK_NULL_HANDLE;
    VkImageView DepthView = VK_NULL_HANDLE;
    VkExtent2D Extent{};
    // The layout the store believes the colour image is in. The caller updates
    // it after transitioning, so the next frame's transition names the right
    // old layout instead of discarding contents.
    VkImageLayout* ColorLayout = nullptr;
    // UINT32_MAX unless the descriptor asked for Bindless.
    std::uint32_t BindlessIndex = UINT32_MAX;
};

// -- Policy, split out so it is testable without a device --------------------

// Which slot a frame renders into. Never slot 0 on a bad index: that is the
// slot most likely to still be in flight, which is what made the old fold
// corrupt rather than degrade.
[[nodiscard]] constexpr std::uint32_t ResolveTargetSlot(std::uint32_t frameInFlightIndex,
                                                        std::uint32_t slotCount)
{
    if (slotCount == 0)
        return 0;
    return frameInFlightIndex < slotCount ? frameInFlightIndex : slotCount - 1;
}

// Whether a slot built for `built` can serve `wanted`. Extent and formats are
// baked into the images, so any difference is a recreate; a zero extent is not
// renderable at all.
[[nodiscard]] constexpr bool TargetSlotNeedsRebuild(const RenderTargetDesc& built,
                                                    const RenderTargetDesc& wanted,
                                                    bool hasImages)
{
    if (wanted.Extent.width == 0 || wanted.Extent.height == 0)
        return false; // nothing to build; Acquire reports it as unavailable
    if (!hasImages)
        return true;
    return built.Extent.width != wanted.Extent.width
        || built.Extent.height != wanted.Extent.height
        || built.ColorFormat != wanted.ColorFormat
        || built.DepthFormat != wanted.DepthFormat
        || built.Read != wanted.Read;
}

class RenderTargetStore
{
public:
    void Setup(const RendererServices& services);
    void Teardown();

    // Selects this frame's slot and advances the retirement clock the store
    // frees against. Call once per frame, before any Acquire.
    void BeginFrame(std::uint32_t frameInFlightIndex, GpuFrameRetirement retirement);

    [[nodiscard]] RenderTargetId Create(const RenderTargetDesc& desc);
    void Destroy(RenderTargetId id);

    // Requests a size for subsequent frames. A change takes effect the next
    // time the affected slot is acquired, so a resize costs one recreate per
    // slot rather than one per frame.
    void SetExtent(RenderTargetId id, VkExtent2D extent);

    // This frame's images, building them if the descriptor changed. nullopt
    // when the id is stale, the extent is still zero, or creation failed.
    [[nodiscard]] std::optional<RenderTargetView> Acquire(RenderTargetId id);

    // This frame's images only if they already exist; never builds.
    //
    // For readers rather than renderers. A reader that builds would hand back
    // an image in UNDEFINED layout that nothing has written, and sampling that
    // is a validation error, not merely a blank frame -- which is exactly what
    // happens when the UI asks for a viewport's texture in the same frame the
    // panel first appeared, before the render side has had a size to work with.
    [[nodiscard]] std::optional<RenderTargetView> Peek(RenderTargetId id);

    [[nodiscard]] std::size_t ResidentCount() const;

private:
    struct Slot
    {
        ImageHandle Color;
        ImageHandle Depth;
        BindlessImageIndex Bindless;
        VkImageLayout ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // The descriptor these images were built for.
        RenderTargetDesc Built;
    };

    struct Target
    {
        RenderTargetDesc Desc;
        std::array<Slot, kMaxFramesInFlight> Slots{};
        std::uint32_t Generation = 0;
        bool Alive = false;
    };

    [[nodiscard]] Target* Resolve(RenderTargetId id);
    void BuildSlot(Slot& slot, const RenderTargetDesc& desc);
    void ReleaseSlot(Slot& slot);

    RendererServices Services{};
    // Index 0 is the reserved null slot, matching every other handle pool.
    std::vector<Target> Targets{ Target{} };
    std::vector<std::uint32_t> FreeList;
    GpuFrameRetirement Retirement;
    std::uint32_t CurrentSlot = 0;
};
