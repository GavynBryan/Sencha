#include <graphics/vulkan/PresentationTarget.h>

#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <platform/SdlWindow.h>

PresentationTarget::PresentationTarget(const Services& services, std::unique_ptr<VulkanSurfaceService> surface,
                                       SdlWindow& window, const PresentationDesc& desc,
                                       std::uint32_t framesInFlight)
    : Log(services.Logging->GetLogger<PresentationTarget>())
    , Device(services.Device->GetDevice())
    , OwnerWindow(&window)
    , Description(desc)
    , Surface(std::move(surface))
{
    if (Surface == nullptr || !Surface->IsValid())
    {
        Log.Error("Cannot create a presentation: the window has no surface");
        return;
    }
    const std::optional<uint32_t> presentFamily = services.Queues->GetQueueFamilies().Present;
    if (!presentFamily.has_value()
        || !Surface->SupportsPresent(services.PhysicalDevice->GetPhysicalDevice(), *presentFamily))
    {
        Log.Error("Cannot create a presentation: the present queue cannot present to this window");
        return;
    }

    Chain = std::make_unique<VulkanSwapchainService>(*services.Logging, *services.Device, *services.PhysicalDevice,
                                                     *Surface, *services.Queues, window.GetExtent());
    if (!Chain->IsValid())
        return;
    if (desc.DepthStencil)
        DepthAttachment = std::make_unique<VulkanDepthTarget>(*services.Images, *services.PhysicalDevice);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    ImageAvailable.assign(framesInFlight, VK_NULL_HANDLE);
    Presents.assign(framesInFlight, PresentRecord{});
    for (VkSemaphore& semaphore : ImageAvailable)
    {
        if (vkCreateSemaphore(Device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
        {
            Log.Error("Failed to create a presentation's acquire semaphore");
            return;
        }
    }
    Valid = CreateImageState();
}

PresentationTarget::~PresentationTarget()
{
    DestroyImageState();
    for (VkSemaphore semaphore : ImageAvailable)
        if (semaphore != VK_NULL_HANDLE)
            vkDestroySemaphore(Device, semaphore, nullptr);
    // The depth image goes through the image service's deletion queue; the
    // swapchain must die before its surface, and the surface before the window.
    DepthAttachment.reset();
    Chain.reset();
    Surface.reset();
}

PresentationAvailability PresentationTarget::Availability() const
{
    return ClassifyPresentation(OwnerWindow->IsMinimized(), OwnerWindow->GetExtent(), RebuildPending);
}

bool PresentationTarget::ExtentChanged() const
{
    const WindowExtent window = OwnerWindow->GetExtent();
    const VkExtent2D chain = Chain->GetExtent();
    return window.Width != chain.width || window.Height != chain.height;
}

bool PresentationTarget::Rebuild(WindowExtent extent)
{
    if (!Chain->Recreate(extent))
        return false;
    // The recreate idled the device, so nothing is still using the old
    // chain's per-image objects.
    DestroyImageState();
    for (PresentRecord& present : Presents)
        present = {};
    DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    RebuildPending = false;
    Valid = CreateImageState();
    return Valid;
}

PresentationTarget::ImageState* PresentationTarget::Image(std::uint32_t imageIndex)
{
    return imageIndex < Images.size() ? &Images[imageIndex] : nullptr;
}

bool PresentationTarget::CreateImageState()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    Images.assign(Chain->GetImageCount(), ImageState{});
    for (ImageState& image : Images)
    {
        if (vkCreateSemaphore(Device, &semaphoreInfo, nullptr, &image.RenderFinished) != VK_SUCCESS)
        {
            Log.Error("Failed to create a presentation's render-finished semaphore");
            return false;
        }
    }
    return true;
}

void PresentationTarget::DestroyImageState()
{
    for (ImageState& image : Images)
        if (image.RenderFinished != VK_NULL_HANDLE)
            vkDestroySemaphore(Device, image.RenderFinished, nullptr);
    Images.clear();
}
