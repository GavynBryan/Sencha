#pragma once

#include "viewport/ViewportId.h"

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanImageService.h>

#include <imgui.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Per-viewport offscreen color (+depth) targets, so each editor viewport renders
// into its own HDR texture shown in the UI via ImGui::Image. Color targets are
// per-frame-in-flight: frames overlap on the GPU, so frame N's ImGui sampling of a
// target must not race frame N+1's render into it. RGBA16F gives bloom headroom; the
// sRGB swapchain store does the single linear->display encode when ImGui samples it.
//
// The render side (Offscreen phase) calls BeginFrame then AcquireForRender per
// viewport; the UI side (panel draw) calls Display to record the on-screen size and
// fetch the current texture. There is a one-frame size lag (the render uses the size
// the panel recorded last frame), the same lag the screen-rect rendering already had.
class ViewportTargetCache
{
public:
    void Setup(const RendererServices& services);
    void Teardown();

    struct RenderView
    {
        VkImage       ColorImage = VK_NULL_HANDLE;
        VkImage       DepthImage = VK_NULL_HANDLE;
        VkImageView   ColorView = VK_NULL_HANDLE;
        VkImageView   DepthView = VK_NULL_HANDLE;
        VkExtent2D    Extent{};
        VkImageLayout* ColorLayout = nullptr; // caller updates after its transitions
        // Bloom scratch: two full-res ping-pong targets + the bindless sample indices.
        VkImage        BloomImage[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkImageView    BloomView[2]{ VK_NULL_HANDLE, VK_NULL_HANDLE };
        VkImageLayout* BloomLayout[2]{ nullptr, nullptr };
        VkExtent2D     BloomExtent{};
        uint32_t       BloomBindless[2]{ UINT32_MAX, UINT32_MAX };
    };

    // -- Render side (Offscreen phase), in this order ------------------------
    void BeginFrame(uint32_t frameInFlightIndex);
    // Ensure the current slot's target for `id` matches its requested size (creating
    // or resizing as needed). nullopt until the panel has requested a size.
    [[nodiscard]] std::optional<RenderView> AcquireForRender(ViewportId id);
    // Destroy targets for viewports not in `live` (closed or re-laid-out).
    void Prune(std::span<const ViewportId> live);

    // -- UI side (panel draw) ------------------------------------------------
    // Record the on-screen pixel size and return the current slot's texture to
    // display (0 until the first render fills it).
    [[nodiscard]] ImTextureID Display(ViewportId id, VkExtent2D extent);

private:
    static constexpr uint32_t kMaxSlots = 4; // upper bound on frames in flight

    struct Slot
    {
        ImageHandle     Color{};
        ImageHandle     Depth{};
        ImageHandle     Bloom[2]{};
        VkExtent2D      Extent{};
        VkExtent2D      BloomExtent{};
        VkDescriptorSet ImGuiSet = VK_NULL_HANDLE;
        VkImageLayout   ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout   BloomLayout[2]{ VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED };
        BindlessImageIndex BloomBindless[2]{};
    };
    struct Target
    {
        ViewportId Id{};
        VkExtent2D Requested{};
        std::array<Slot, kMaxSlots> Slots{};
    };

    [[nodiscard]] Target* Find(ViewportId id);
    [[nodiscard]] Target& FindOrAdd(ViewportId id);
    void ResizeSlot(Slot& slot, VkExtent2D extent);
    void DestroySlot(Slot& slot);
    // ImGui_ImplVulkan_RemoveTexture frees the set immediately, so a set retired on
    // resize must outlive any in-flight frame still sampling it. Free eligible ones
    // (or all, on teardown when the device is idle).
    void FlushRetiredSets(bool force);

    struct RetiredSet
    {
        VkDescriptorSet Set = VK_NULL_HANDLE;
        uint64_t RetireAfter = 0;
    };

    RendererServices Services{};
    std::vector<Target> Targets;
    std::vector<RetiredSet> RetiredSets;
    uint64_t FrameCounter = 0;
    uint32_t CurrentSlot = 0;
    static constexpr uint64_t kRetireFrames = kMaxSlots; // > frames in flight
};
