#include <graphics/vulkan/VulkanSwapchainService.h>

#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanPhysicalDeviceService.h>
#include <graphics/vulkan/VulkanQueueService.h>
#include <graphics/vulkan/VulkanSurfaceService.h>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace
{
    const char* PresentModeName(VkPresentModeKHR mode)
    {
        switch (mode)
        {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
        default: return "UNKNOWN";
        }
    }
}

VulkanSwapchainService::VulkanSwapchainService(
    LoggingProvider& logging,
    VulkanDeviceService& device,
    VulkanPhysicalDeviceService& physicalDevice,
    VulkanSurfaceService& surface,
    VulkanQueueService& queues,
    WindowExtent desiredExtent)
    : Log(logging.GetLogger<VulkanSwapchainService>())
    , PhysicalDevice(physicalDevice.GetPhysicalDevice())
    , Device(device.GetDevice())
    , Surface(surface.GetSurface())
    , Queues(queues)
{
    if (!device.IsValid())
    {
        Log.Error("Cannot create Vulkan swapchain: VulkanDeviceService is not valid");
        return;
    }

    if (!physicalDevice.IsValid())
    {
        Log.Error("Cannot create Vulkan swapchain: VulkanPhysicalDeviceService is not valid");
        return;
    }

    if (!surface.IsValid())
    {
        Log.Error("Cannot create Vulkan swapchain: VulkanSurfaceService is not valid");
        return;
    }

    if (!queues.IsValid() || !queues.GetQueueFamilies().HasGraphics()
        || !queues.GetQueueFamilies().HasPresent())
    {
        Log.Error("Cannot create Vulkan swapchain: graphics and present queues are required");
        return;
    }

    CreateSwapchain(desiredExtent);
}

VulkanSwapchainService::~VulkanSwapchainService()
{
    DestroySwapchain();
}

VkImage VulkanSwapchainService::GetImage(uint32_t index) const
{
    return index < Images.size() ? Images[index] : VK_NULL_HANDLE;
}

VkImageView VulkanSwapchainService::GetImageView(uint32_t index) const
{
    return index < ImageViews.size() ? ImageViews[index] : VK_NULL_HANDLE;
}

SwapchainState VulkanSwapchainService::GetState() const
{
    return SwapchainState{
        .Generation = Generation,
        .Extent = Extent,
        .PresentMode = PresentMode,
        .ImageCount = static_cast<uint32_t>(Images.size()),
        .MinImageCount = LastMinImageCount,
        .MaxImageCount = LastMaxImageCount,
        .Format = Format,
        .ColorSpace = ColorSpace,
    };
}

bool VulkanSwapchainService::Recreate(WindowExtent desiredExtent)
{
    if (Device == VK_NULL_HANDLE)
    {
        return false;
    }

    vkDeviceWaitIdle(Device);
    DestroySwapchain();
    const bool created = CreateSwapchain(desiredExtent);
    if (created)
        ++RecreateCount;
    return created;
}

VulkanSwapchainService::SwapchainSupport VulkanSwapchainService::QuerySupport() const
{
    SwapchainSupport support;

    VkResult capabilitiesResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        PhysicalDevice,
        Surface,
        &support.Capabilities);
    if (capabilitiesResult != VK_SUCCESS)
    {
        Log.Error("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed with code {}",
                  static_cast<int>(capabilitiesResult));
        return support;
    }

    uint32_t formatCount = 0;
    VkResult formatCountResult = vkGetPhysicalDeviceSurfaceFormatsKHR(
        PhysicalDevice,
        Surface,
        &formatCount,
        nullptr);
    if (formatCountResult != VK_SUCCESS)
    {
        Log.Error("vkGetPhysicalDeviceSurfaceFormatsKHR failed with code {}",
                  static_cast<int>(formatCountResult));
        return support;
    }

    if (formatCount > 0)
    {
        support.Formats.resize(formatCount);
        VkResult formatResult = vkGetPhysicalDeviceSurfaceFormatsKHR(
            PhysicalDevice,
            Surface,
            &formatCount,
            support.Formats.data());
        if (formatResult != VK_SUCCESS)
        {
            Log.Error("vkGetPhysicalDeviceSurfaceFormatsKHR failed with code {}",
                      static_cast<int>(formatResult));
            support.Formats.clear();
            return support;
        }
    }

    uint32_t presentModeCount = 0;
    VkResult presentModeCountResult = vkGetPhysicalDeviceSurfacePresentModesKHR(
        PhysicalDevice,
        Surface,
        &presentModeCount,
        nullptr);
    if (presentModeCountResult != VK_SUCCESS)
    {
        Log.Error("vkGetPhysicalDeviceSurfacePresentModesKHR failed with code {}",
                  static_cast<int>(presentModeCountResult));
        return support;
    }

    if (presentModeCount > 0)
    {
        support.PresentModes.resize(presentModeCount);
        VkResult presentModeResult = vkGetPhysicalDeviceSurfacePresentModesKHR(
            PhysicalDevice,
            Surface,
            &presentModeCount,
            support.PresentModes.data());
        if (presentModeResult != VK_SUCCESS)
        {
            Log.Error("vkGetPhysicalDeviceSurfacePresentModesKHR failed with code {}",
                      static_cast<int>(presentModeResult));
            support.PresentModes.clear();
            return support;
        }
    }

    support.Valid = true;
    return support;
}

bool VulkanSwapchainService::HasUsableSupport(const SwapchainSupport& support) const
{
    constexpr VkImageUsageFlags requiredUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    return support.Valid && !support.Formats.empty() && !support.PresentModes.empty()
        && support.Capabilities.minImageCount > 0
        && (support.Capabilities.supportedUsageFlags & requiredUsage) == requiredUsage;
}

VkSurfaceFormatKHR VulkanSwapchainService::ChooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const
{
    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    return formats.front();
}

VkPresentModeKHR VulkanSwapchainService::ChoosePresentMode(
    const std::vector<VkPresentModeKHR>& presentModes) const
{
    if (const char* requested = std::getenv("SENCHA_PRESENT_MODE"))
    {
        struct PresentModeOverride
        {
            std::string_view Name;
            VkPresentModeKHR Mode;
        };

        constexpr PresentModeOverride overrides[] = {
            { "IMMEDIATE", VK_PRESENT_MODE_IMMEDIATE_KHR },
            { "MAILBOX", VK_PRESENT_MODE_MAILBOX_KHR },
            { "FIFO", VK_PRESENT_MODE_FIFO_KHR },
            { "FIFO_RELAXED", VK_PRESENT_MODE_FIFO_RELAXED_KHR },
        };

        for (const auto& overrideMode : overrides)
        {
            if (overrideMode.Name == requested)
            {
                auto found = std::find(presentModes.begin(), presentModes.end(), overrideMode.Mode);
                if (found != presentModes.end())
                {
                    Log.Info("Using requested Vulkan present mode {}", overrideMode.Name);
                    return overrideMode.Mode;
                }

                Log.Warn("Requested Vulkan present mode {} is not available", overrideMode.Name);
                break;
            }
        }
    }

    auto iter = std::find(
        presentModes.begin(),
        presentModes.end(),
        VK_PRESENT_MODE_FIFO_KHR);

    return iter != presentModes.end() ? VK_PRESENT_MODE_FIFO_KHR : presentModes.front();
}

VkCompositeAlphaFlagBitsKHR VulkanSwapchainService::ChooseCompositeAlpha(
    VkCompositeAlphaFlagsKHR supportedFlags) const
{
    if (supportedFlags & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
    {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    if (supportedFlags & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
    {
        return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }

    if (supportedFlags & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
    {
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
}

VkExtent2D VulkanSwapchainService::ChooseExtent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    WindowExtent desiredExtent) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    VkExtent2D chosen{
        desiredExtent.Width,
        desiredExtent.Height
    };

    chosen.width = std::clamp(
        chosen.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    chosen.height = std::clamp(
        chosen.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);

    return chosen;
}

uint32_t VulkanSwapchainService::ChooseImageCount(
    const VkSurfaceCapabilitiesKHR& capabilities) const
{
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0)
    {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    return imageCount;
}

bool VulkanSwapchainService::CreateSwapchain(WindowExtent desiredExtent)
{
    if (desiredExtent.Width == 0 || desiredExtent.Height == 0)
    {
        Log.Warn("Cannot create Vulkan swapchain for zero-sized extent");
        return false;
    }

    auto support = QuerySupport();
    if (!HasUsableSupport(support))
    {
        Log.Error("Cannot create Vulkan swapchain: surface support is incomplete");
        return false;
    }

    const auto surfaceFormat = ChooseSurfaceFormat(support.Formats);
    const auto presentMode = ChoosePresentMode(support.PresentModes);
    const auto compositeAlpha = ChooseCompositeAlpha(support.Capabilities.supportedCompositeAlpha);
    const auto extent = ChooseExtent(support.Capabilities, desiredExtent);
    const auto imageCount = ChooseImageCount(support.Capabilities);

    if (extent.width == 0 || extent.height == 0)
    {
        Log.Warn("Cannot create Vulkan swapchain for zero-sized surface extent");
        return false;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createInfo.preTransform = support.Capabilities.currentTransform;
    createInfo.compositeAlpha = compositeAlpha;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    const auto& families = Queues.GetQueueFamilies();
    uint32_t queueFamilyIndices[] = { *families.Graphics, *families.Present };
    if (families.Graphics == families.Present)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }

    VkResult result = vkCreateSwapchainKHR(Device, &createInfo, nullptr, &Swapchain);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateSwapchainKHR failed with code {}", static_cast<int>(result));
        Swapchain = VK_NULL_HANDLE;
        return false;
    }

    uint32_t actualImageCount = 0;
    VkResult countResult = vkGetSwapchainImagesKHR(Device, Swapchain, &actualImageCount, nullptr);
    if (countResult != VK_SUCCESS || actualImageCount == 0)
    {
        Log.Error("vkGetSwapchainImagesKHR failed with code {}", static_cast<int>(countResult));
        DestroySwapchain();
        return false;
    }

    Images.resize(actualImageCount);
    VkResult imageResult = vkGetSwapchainImagesKHR(
        Device,
        Swapchain,
        &actualImageCount,
        Images.data());
    if (imageResult != VK_SUCCESS)
    {
        Log.Error("vkGetSwapchainImagesKHR failed with code {}", static_cast<int>(imageResult));
        DestroySwapchain();
        return false;
    }

    Format = surfaceFormat.format;
    ColorSpace = surfaceFormat.colorSpace;
    PresentMode = presentMode;
    Extent = extent;
    LastMinImageCount = support.Capabilities.minImageCount;
    LastMaxImageCount = support.Capabilities.maxImageCount;
    ++Generation;

    if (!CreateImageViews())
    {
        DestroySwapchain();
        return false;
    }

    Log.Info("Vulkan swapchain created: {}x{} images:{} present:{} format:{} colorSpace:{} minImages:{} maxImages:{}",
             Extent.width,
             Extent.height,
             Images.size(),
             PresentModeName(PresentMode),
             static_cast<int>(Format),
             static_cast<int>(ColorSpace),
             LastMinImageCount,
             LastMaxImageCount);
    return true;
}

bool VulkanSwapchainService::CreateImageViews()
{
    ImageViews.resize(Images.size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < Images.size(); ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = Images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = Format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(Device, &viewInfo, nullptr, &ImageViews[i]);
        if (result != VK_SUCCESS)
        {
            Log.Error("vkCreateImageView failed with code {}", static_cast<int>(result));
            return false;
        }
    }

    return true;
}

void VulkanSwapchainService::DestroySwapchain()
{
    for (auto view : ImageViews)
    {
        if (view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(Device, view, nullptr);
        }
    }

    ImageViews.clear();
    Images.clear();

    if (Swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(Device, Swapchain, nullptr);
        Swapchain = VK_NULL_HANDLE;
        Log.Info("Vulkan swapchain destroyed");
    }

    Format = VK_FORMAT_UNDEFINED;
    ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    PresentMode = VK_PRESENT_MODE_FIFO_KHR;
    Extent = {};
}
