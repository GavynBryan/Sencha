#include <core/logging/ConsoleLogSink.h>
#include <window/SdlVideoService.h>
#include <window/SdlWindow.h>
#include <window/SdlWindowService.h>
#include <core/service/ServiceHost.h>
#include <render/backend/vulkan/VulkanBarriers.h>
#include <render/backend/vulkan/VulkanBootstrapPolicy.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanFrameService.h>
#include <render/backend/vulkan/VulkanInstanceService.h>
#include <render/backend/vulkan/VulkanPhysicalDeviceService.h>
#include <render/backend/vulkan/VulkanQueueService.h>
#include <render/backend/vulkan/VulkanSurfaceService.h>
#include <render/backend/vulkan/VulkanSwapchainService.h>
#include <window/WindowCreateInfo.h>

#include <SDL3/SDL.h>

#include <vector>

class SdlVulkanWindowExample
{
};

namespace
{
    void RecordClearFrame(const VulkanFrame& frame,
                          std::vector<VkImageLayout>& imageLayouts)
    {
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (frame.ImageIndex < imageLayouts.size())
        {
            oldLayout = imageLayouts[frame.ImageIndex];
        }

        VulkanBarriers::TransitionForClear(frame.CommandBuffer, frame.SwapchainImage, oldLayout);

        VkClearColorValue clearColor{ { 0.05f, 0.09f, 0.12f, 1.0f } };
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;

        vkCmdClearColorImage(
            frame.CommandBuffer,
            frame.SwapchainImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            &clearColor,
            1,
            &range);

        VulkanBarriers::TransitionForPresent(
            frame.CommandBuffer,
            frame.SwapchainImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        if (frame.ImageIndex < imageLayouts.size())
        {
            imageLayouts[frame.ImageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
    }

    bool RecreatePresentSurface(Logger& logger,
                                VulkanSwapchainService& swapchain,
                                VulkanFrameService& frames,
                                WindowExtent extent,
                                std::vector<VkImageLayout>& imageLayouts)
    {
        if (extent.Width == 0 || extent.Height == 0)
        {
            return false;
        }

        if (!swapchain.Recreate(extent))
        {
            logger.Error("Failed to recreate Vulkan swapchain");
            return false;
        }

        frames.ResetAfterSwapchainRecreate();
        imageLayouts.assign(swapchain.GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        return true;
    }

    bool IsRenderableExtent(WindowExtent extent)
    {
        return extent.Width > 0 && extent.Height > 0;
    }
}

int main()
{
    ServiceHost services;
    auto& logging = services.GetLoggingProvider();
    logging.AddSink<ConsoleLogSink>();

    auto& logger = logging.GetLogger<SdlVulkanWindowExample>();

    WindowCreateInfo windowInfo;
    windowInfo.Title = "Sencha SDL3 Vulkan Window";
    windowInfo.Width = 1280;
    windowInfo.Height = 720;
    windowInfo.GraphicsApi = WindowGraphicsApi::Vulkan;
    windowInfo.Resizable = true;
    windowInfo.Visible = true;

    SdlVideoService video(logging);
    if (!video.IsValid())
    {
        return 1;
    }

    SdlWindowService windows(logging, video);
    SdlWindow* window = windows.CreateWindow(windowInfo);
    if (!window)
    {
        return 1;
    }

    auto windowId = window->GetId();

    VulkanBootstrapPolicy vulkanPolicy;
    vulkanPolicy.AppName = "SdlVulkanWindow";
    vulkanPolicy.RequiredQueues.Present = true;
    vulkanPolicy.RequiredDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    auto platformExtensions = windows.GetRequiredVulkanInstanceExtensions();
    vulkanPolicy.RequiredInstanceExtensions.insert(
        vulkanPolicy.RequiredInstanceExtensions.end(),
        platformExtensions.begin(),
        platformExtensions.end());

    VulkanInstanceService instance(logging, vulkanPolicy);
    if (!instance.IsValid())
    {
        return 1;
    }

    VulkanSurfaceService surface(logging, instance, *window);
    if (!surface.IsValid())
    {
        return 1;
    }

    VulkanPhysicalDeviceService physicalDevice(logging, instance, vulkanPolicy, &surface);
    if (!physicalDevice.IsValid())
    {
        return 1;
    }

    VulkanDeviceService device(logging, physicalDevice, vulkanPolicy);
    if (!device.IsValid())
    {
        return 1;
    }

    VulkanQueueService queues(logging, device, physicalDevice, vulkanPolicy);
    if (!queues.IsValid())
    {
        return 1;
    }

    VulkanSwapchainService swapchain(logging, device, physicalDevice, surface, queues, window->GetExtent());
    if (!swapchain.IsValid())
    {
        return 1;
    }

    VulkanFrameService frames(logging, device, queues, swapchain);
    if (!frames.IsValid())
    {
        return 1;
    }

    std::vector<VkImageLayout> imageLayouts(
        swapchain.GetImageCount(),
        VK_IMAGE_LAYOUT_UNDEFINED);

    logger.Info("SDL3 Vulkan present-frame substrate is running. Close the window to exit.");

    while (windows.IsAlive(windowId))
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            windows.HandleEvent(event);
        }

        WindowExtent resizeExtent{};
        const bool resizePending = windows.ConsumeResize(windowId, &resizeExtent);
        const auto* state = windows.GetState(windowId);
        const bool minimized = state && state->Minimized;
        WindowExtent currentExtent = resizePending ? resizeExtent : windows.GetExtent(windowId);

        if (minimized || !IsRenderableExtent(currentExtent))
        {
            SDL_Delay(16);
            continue;
        }

        if (resizePending)
        {
            RecreatePresentSurface(logger, swapchain, frames, currentExtent, imageLayouts);
            continue;
        }

        VulkanFrame frame;
        VulkanFrameStatus beginStatus = frames.BeginFrame(frame);
        if (beginStatus == VulkanFrameStatus::SwapchainOutOfDate
            || beginStatus == VulkanFrameStatus::SurfaceUnavailable)
        {
            RecreatePresentSurface(logger, swapchain, frames, currentExtent, imageLayouts);
            continue;
        }
        if (beginStatus == VulkanFrameStatus::DeviceLost || beginStatus == VulkanFrameStatus::Error)
        {
            return 1;
        }

        RecordClearFrame(frame, imageLayouts);

        VulkanFrameStatus endStatus = frames.EndFrame(frame);
        if (endStatus == VulkanFrameStatus::SwapchainOutOfDate
            || endStatus == VulkanFrameStatus::SurfaceUnavailable)
        {
            RecreatePresentSurface(logger, swapchain, frames, currentExtent, imageLayouts);
            continue;
        }
        if (endStatus == VulkanFrameStatus::DeviceLost || endStatus == VulkanFrameStatus::Error)
        {
            return 1;
        }
    }

    return 0;
}
