#pragma once

#include <core/logging/LoggingProvider.h>
#include <graphics/vulkan/PresentationFramePlan.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanDepthTarget.h>
#include <graphics/vulkan/VulkanSurfaceService.h>
#include <graphics/vulkan/VulkanSwapchainService.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

class SdlWindow;
class VulkanDeviceService;
class VulkanImageService;
class VulkanPhysicalDeviceService;
class VulkanQueueService;

// What a presentation's swapchain scope binds besides colour. Pipelines bake
// their attachment formats at Setup, so this is fixed when the presentation is
// created; the depth image itself is made the first frame the scope records.
struct PresentationDesc
{
    bool DepthStencil = true;
};

// Everything that exists once per window the renderer presents to: its
// surface and swapchain, the semaphores and per-image state that pace them,
// and the depth attachment of its swapchain scope. The window outlives it.
class PresentationTarget
{
public:
    struct Services
    {
        LoggingProvider* Logging = nullptr;
        VulkanDeviceService* Device = nullptr;
        VulkanPhysicalDeviceService* PhysicalDevice = nullptr;
        VulkanQueueService* Queues = nullptr;
        VulkanImageService* Images = nullptr;
    };

    PresentationTarget(const Services& services, std::unique_ptr<VulkanSurfaceService> surface,
                       SdlWindow& window, const PresentationDesc& desc, std::uint32_t framesInFlight);
    // The device must be done with every frame that used this presentation.
    ~PresentationTarget();

    PresentationTarget(const PresentationTarget&) = delete;
    PresentationTarget& operator=(const PresentationTarget&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }
    [[nodiscard]] SdlWindow& Window() const { return *OwnerWindow; }
    [[nodiscard]] VulkanSwapchainService& Swapchain() const { return *Chain; }
    [[nodiscard]] const PresentationDesc& Desc() const { return Description; }

    [[nodiscard]] PresentationAvailability Availability() const;
    // True when the window's size moved away from the swapchain's.
    [[nodiscard]] bool ExtentChanged() const;
    void MarkNeedsRebuild() { RebuildPending = true; }
    // Recreates the swapchain at `extent` and forgets per-image state.
    bool Rebuild(WindowExtent extent);

    struct ImageState
    {
        VkSemaphore RenderFinished = VK_NULL_HANDLE;
        // The frame slot fence that last rendered into this image.
        VkFence InFlightFence = VK_NULL_HANDLE;
        VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    [[nodiscard]] VkSemaphore AcquireSemaphore(std::uint32_t frameSlot) const { return ImageAvailable[frameSlot]; }
    [[nodiscard]] ImageState* Image(std::uint32_t imageIndex);

    // Present-wait bookkeeping, per frame slot, for the pacing presentation.
    struct PresentRecord
    {
        std::uint64_t Id = 0;
        std::uint64_t SwapchainGeneration = 0;
    };
    [[nodiscard]] PresentRecord& LastPresent(std::uint32_t frameSlot) { return Presents[frameSlot]; }

    // Null for a presentation declared without one.
    [[nodiscard]] VulkanDepthTarget* Depth() { return DepthAttachment.get(); }
    VkImageLayout DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // What features recording into this presentation's swapchain scope are
    // set up with: its swapchain and its attachment formats. Stable for the
    // presentation's life, so a feature may keep the pointer.
    RendererServices SwapchainFeatureServices{};

private:
    bool CreateImageState();
    void DestroyImageState();

    Logger& Log;
    VkDevice Device = VK_NULL_HANDLE;
    SdlWindow* OwnerWindow = nullptr;
    PresentationDesc Description;
    std::unique_ptr<VulkanSurfaceService> Surface;
    std::unique_ptr<VulkanSwapchainService> Chain;
    std::unique_ptr<VulkanDepthTarget> DepthAttachment;
    std::vector<VkSemaphore> ImageAvailable;
    std::vector<PresentRecord> Presents;
    std::vector<ImageState> Images;
    bool RebuildPending = false;
    bool Valid = false;
};
