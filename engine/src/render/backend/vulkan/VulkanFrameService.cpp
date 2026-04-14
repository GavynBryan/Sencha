#include <render/backend/vulkan/VulkanFrameService.h>

#include <render/backend/vulkan/VulkanDeletionQueueService.h>
#include <render/backend/vulkan/VulkanDeviceService.h>
#include <render/backend/vulkan/VulkanQueueService.h>
#include <render/backend/vulkan/VulkanSwapchainService.h>

#include <algorithm>
#include <limits>

VulkanFrameService::VulkanFrameService(
    LoggingProvider& logging,
    VulkanDeviceService& device,
    VulkanQueueService& queues,
    VulkanSwapchainService& swapchain,
    VulkanDeletionQueueService& deletionQueue,
    uint32_t framesInFlight)
    : Log(logging.GetLogger<VulkanFrameService>())
    , Device(device.GetDevice())
    , Queues(queues)
    , Swapchain(swapchain)
    , DeletionQueue(&deletionQueue)
{
    if (!device.IsValid())
    {
        Log.Error("Cannot create Vulkan frame service: VulkanDeviceService is not valid");
        return;
    }

    if (!queues.IsValid() || !queues.GetQueueFamilies().HasGraphics()
        || queues.GetGraphicsQueue() == VK_NULL_HANDLE)
    {
        Log.Error("Cannot create Vulkan frame service: graphics queue is required");
        return;
    }

    if (!swapchain.IsValid())
    {
        Log.Error("Cannot create Vulkan frame service: VulkanSwapchainService is not valid");
        return;
    }

    Valid = CreateFrameData(std::max(framesInFlight, 1u));
    ResetAfterSwapchainRecreate();

    if (Valid)
    {
        Log.Info("Vulkan frame service created: frames in flight {}", Frames.size());
    }
}

VulkanFrameService::~VulkanFrameService()
{
    DestroyFrameData();
}

VulkanFrameStatus VulkanFrameService::BeginFrame(VulkanFrame& frame)
{
    frame = {};

    if (!Valid)
    {
        return VulkanFrameStatus::Error;
    }

    if (!Swapchain.IsValid() || Swapchain.GetImageCount() == 0)
    {
        return VulkanFrameStatus::SurfaceUnavailable;
    }

    auto& current = Frames[CurrentFrame];

    if (current.Submitted)
    {
        VkResult waitResult = vkWaitForFences(
            Device,
            1,
            &current.InFlightFence,
            VK_TRUE,
            std::numeric_limits<uint64_t>::max());
        if (waitResult == VK_ERROR_DEVICE_LOST)
        {
            Log.Error("vkWaitForFences failed: device lost");
            return VulkanFrameStatus::DeviceLost;
        }
        if (waitResult != VK_SUCCESS)
        {
            Log.Error("vkWaitForFences failed with code {}", static_cast<int>(waitResult));
            return VulkanFrameStatus::Error;
        }

        current.Submitted = false;
        DeletionQueue->AdvanceFrame();
    }

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        Device,
        Swapchain.GetSwapchain(),
        std::numeric_limits<uint64_t>::max(),
        current.ImageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return VulkanFrameStatus::SwapchainOutOfDate;
    }
    if (acquireResult == VK_ERROR_DEVICE_LOST)
    {
        Log.Error("vkAcquireNextImageKHR failed: device lost");
        return VulkanFrameStatus::DeviceLost;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
    {
        Log.Error("vkAcquireNextImageKHR failed with code {}", static_cast<int>(acquireResult));
        return VulkanFrameStatus::Error;
    }
    current.AcquireSuboptimal = acquireResult == VK_SUBOPTIMAL_KHR;

    if (imageIndex >= ImageRenderFinishedSemaphores.size())
    {
        Log.Error("Acquired swapchain image index is outside frame sync state");
        return VulkanFrameStatus::Error;
    }

    if (imageIndex < ImageInFlightFences.size()
        && ImageInFlightFences[imageIndex] != VK_NULL_HANDLE)
    {
        VkResult imageWaitResult = vkWaitForFences(
            Device,
            1,
            &ImageInFlightFences[imageIndex],
            VK_TRUE,
            std::numeric_limits<uint64_t>::max());
        if (imageWaitResult == VK_ERROR_DEVICE_LOST)
        {
            Log.Error("Waiting for swapchain image fence failed: device lost");
            return VulkanFrameStatus::DeviceLost;
        }
        if (imageWaitResult != VK_SUCCESS)
        {
            Log.Error("Waiting for swapchain image fence failed with code {}",
                      static_cast<int>(imageWaitResult));
            return VulkanFrameStatus::Error;
        }
    }

    VkResult resetPoolResult = vkResetCommandPool(Device, current.CommandPool, 0);
    if (resetPoolResult != VK_SUCCESS)
    {
        Log.Error("vkResetCommandPool failed with code {}", static_cast<int>(resetPoolResult));
        return resetPoolResult == VK_ERROR_DEVICE_LOST
            ? VulkanFrameStatus::DeviceLost
            : VulkanFrameStatus::Error;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult beginResult = vkBeginCommandBuffer(current.CommandBuffer, &beginInfo);
    if (beginResult != VK_SUCCESS)
    {
        Log.Error("vkBeginCommandBuffer failed with code {}", static_cast<int>(beginResult));
        return beginResult == VK_ERROR_DEVICE_LOST
            ? VulkanFrameStatus::DeviceLost
            : VulkanFrameStatus::Error;
    }

    frame.FrameIndex = CurrentFrame;
    frame.ImageIndex = imageIndex;
    frame.CommandBuffer = current.CommandBuffer;
    frame.SwapchainImage = Swapchain.GetImage(imageIndex);
    frame.SwapchainImageView = Swapchain.GetImageView(imageIndex);
    frame.SwapchainFormat = Swapchain.GetFormat();
    frame.SwapchainExtent = Swapchain.GetExtent();

    return VulkanFrameStatus::Ready;
}

VulkanFrameStatus VulkanFrameService::EndFrame(const VulkanFrame& frame)
{
    if (!Valid || frame.FrameIndex >= Frames.size())
    {
        return VulkanFrameStatus::Error;
    }

    if (frame.ImageIndex >= ImageRenderFinishedSemaphores.size())
    {
        return VulkanFrameStatus::Error;
    }

    auto& current = Frames[frame.FrameIndex];
    VkSemaphore renderFinished = ImageRenderFinishedSemaphores[frame.ImageIndex];

    VkResult endResult = vkEndCommandBuffer(current.CommandBuffer);
    if (endResult != VK_SUCCESS)
    {
        Log.Error("vkEndCommandBuffer failed with code {}", static_cast<int>(endResult));
        return endResult == VK_ERROR_DEVICE_LOST
            ? VulkanFrameStatus::DeviceLost
            : VulkanFrameStatus::Error;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &current.ImageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &current.CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished;

    VkResult resetFenceResult = vkResetFences(Device, 1, &current.InFlightFence);
    if (resetFenceResult != VK_SUCCESS)
    {
        Log.Error("vkResetFences failed with code {}", static_cast<int>(resetFenceResult));
        return resetFenceResult == VK_ERROR_DEVICE_LOST
            ? VulkanFrameStatus::DeviceLost
            : VulkanFrameStatus::Error;
    }

    VkResult submitResult = vkQueueSubmit(
        Queues.GetGraphicsQueue(),
        1,
        &submitInfo,
        current.InFlightFence);
    if (submitResult != VK_SUCCESS)
    {
        current.Submitted = false;
        Log.Error("vkQueueSubmit failed with code {}", static_cast<int>(submitResult));
        return submitResult == VK_ERROR_DEVICE_LOST
            ? VulkanFrameStatus::DeviceLost
            : VulkanFrameStatus::Error;
    }

    current.Submitted = true;
    if (frame.ImageIndex < ImageInFlightFences.size())
    {
        ImageInFlightFences[frame.ImageIndex] = current.InFlightFence;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    VkSwapchainKHR swapchain = Swapchain.GetSwapchain();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &frame.ImageIndex;

    VkResult presentResult = vkQueuePresentKHR(Queues.GetPresentQueue(), &presentInfo);
    AdvanceFrame();

    const bool acquireSuboptimal = current.AcquireSuboptimal;
    current.AcquireSuboptimal = false;

    if (presentResult == VK_ERROR_DEVICE_LOST)
    {
        Log.Error("vkQueuePresentKHR failed: device lost");
        return VulkanFrameStatus::DeviceLost;
    }

    if (presentResult != VK_SUCCESS
        && presentResult != VK_ERROR_OUT_OF_DATE_KHR
        && presentResult != VK_SUBOPTIMAL_KHR)
    {
        Log.Error("vkQueuePresentKHR failed with code {}", static_cast<int>(presentResult));
        return VulkanFrameStatus::Error;
    }

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR
        || acquireSuboptimal)
    {
        return VulkanFrameStatus::SwapchainOutOfDate;
    }

    return VulkanFrameStatus::Ready;
}

void VulkanFrameService::ResetAfterSwapchainRecreate()
{
    if (Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(Device);
    }

    DestroyImageSyncObjects();
    ImageInFlightFences.assign(Swapchain.GetImageCount(), VK_NULL_HANDLE);
    CurrentFrame = 0;

    for (auto& frame : Frames)
    {
        frame.Submitted = false;
        frame.AcquireSuboptimal = false;
    }

    if (Swapchain.GetImageCount() > 0 && !CreateImageSyncObjects())
    {
        Valid = false;
    }
}

bool VulkanFrameService::CreateFrameData(uint32_t framesInFlight)
{
    Frames.resize(framesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    const uint32_t graphicsFamily = *Queues.GetQueueFamilies().Graphics;

    for (auto& frame : Frames)
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily;

        VkResult poolResult = vkCreateCommandPool(Device, &poolInfo, nullptr, &frame.CommandPool);
        if (poolResult != VK_SUCCESS)
        {
            Log.Error("vkCreateCommandPool failed with code {}", static_cast<int>(poolResult));
            return false;
        }

        VkCommandBufferAllocateInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandInfo.commandPool = frame.CommandPool;
        commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandInfo.commandBufferCount = 1;

        VkResult commandResult = vkAllocateCommandBuffers(
            Device,
            &commandInfo,
            &frame.CommandBuffer);
        if (commandResult != VK_SUCCESS)
        {
            Log.Error("vkAllocateCommandBuffers failed with code {}", static_cast<int>(commandResult));
            return false;
        }

        VkResult imageSemaphoreResult = vkCreateSemaphore(
            Device,
            &semaphoreInfo,
            nullptr,
            &frame.ImageAvailable);
        VkResult fenceResult = vkCreateFence(Device, &fenceInfo, nullptr, &frame.InFlightFence);

        if (imageSemaphoreResult != VK_SUCCESS || fenceResult != VK_SUCCESS)
        {
            Log.Error("Failed to create Vulkan frame synchronization objects");
            return false;
        }
    }

    return true;
}

bool VulkanFrameService::CreateImageSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    ImageRenderFinishedSemaphores.assign(Swapchain.GetImageCount(), VK_NULL_HANDLE);
    for (auto& semaphore : ImageRenderFinishedSemaphores)
    {
        VkResult result = vkCreateSemaphore(Device, &semaphoreInfo, nullptr, &semaphore);
        if (result != VK_SUCCESS)
        {
            Log.Error("Failed to create swapchain image render-finished semaphore");
            DestroyImageSyncObjects();
            return false;
        }
    }

    return true;
}

void VulkanFrameService::DestroyImageSyncObjects()
{
    for (auto semaphore : ImageRenderFinishedSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(Device, semaphore, nullptr);
        }
    }

    ImageRenderFinishedSemaphores.clear();
}

void VulkanFrameService::DestroyFrameData()
{
    if (Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(Device);
    }

    DestroyImageSyncObjects();

    for (auto& frame : Frames)
    {
        if (frame.ImageAvailable != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(Device, frame.ImageAvailable, nullptr);
            frame.ImageAvailable = VK_NULL_HANDLE;
        }

        if (frame.InFlightFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(Device, frame.InFlightFence, nullptr);
            frame.InFlightFence = VK_NULL_HANDLE;
        }

        if (frame.CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(Device, frame.CommandPool, nullptr);
            frame.CommandPool = VK_NULL_HANDLE;
            frame.CommandBuffer = VK_NULL_HANDLE;
        }
    }

    Frames.clear();
    ImageInFlightFences.clear();
    Valid = false;
}

void VulkanFrameService::AdvanceFrame()
{
    CurrentFrame = (CurrentFrame + 1) % static_cast<uint32_t>(Frames.size());
}
