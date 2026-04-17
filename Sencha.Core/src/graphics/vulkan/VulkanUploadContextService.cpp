#include <graphics/vulkan/VulkanUploadContextService.h>

#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanQueueService.h>

#include <climits>

VulkanUploadContextService::VulkanUploadContextService(
    LoggingProvider& logging,
    VulkanDeviceService& device,
    VulkanQueueService& queues)
    : Log(logging.GetLogger<VulkanUploadContextService>())
    , Device(device.GetDevice())
    , UploadQueue(queues.GetGraphicsQueue())
{
    if (!device.IsValid() || UploadQueue == VK_NULL_HANDLE)
    {
        Log.Error("VulkanUploadContextService: upstream Vulkan services not valid");
        return;
    }

    const auto& families = queues.GetQueueFamilies();
    if (!families.HasGraphics())
    {
        Log.Error("VulkanUploadContextService requires a graphics queue family");
        return;
    }
    UploadQueueFamily = *families.Graphics;

    if (!CreateResources())
    {
        Log.Error("Failed to create upload command pool / fence");
        DestroyResources();
    }
}

VulkanUploadContextService::~VulkanUploadContextService()
{
    DestroyResources();
}

bool VulkanUploadContextService::CreateResources()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
                   | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = UploadQueueFamily;

    VkResult result = vkCreateCommandPool(Device, &poolInfo, nullptr, &UploadPool);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateCommandPool (upload) failed ({})", static_cast<int>(result));
        return false;
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    result = vkCreateFence(Device, &fenceInfo, nullptr, &UploadFence);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateFence (upload) failed ({})", static_cast<int>(result));
        return false;
    }

    return true;
}

void VulkanUploadContextService::DestroyResources()
{
    if (UploadFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(Device, UploadFence, nullptr);
        UploadFence = VK_NULL_HANDLE;
    }
    if (UploadPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(Device, UploadPool, nullptr);
        UploadPool = VK_NULL_HANDLE;
    }
}

VkCommandBuffer VulkanUploadContextService::Begin()
{
    if (UploadPool == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkCommandBufferAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.commandPool = UploadPool;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult result = vkAllocateCommandBuffers(Device, &info, &cmd);
    if (result != VK_SUCCESS)
    {
        Log.Error("upload: vkAllocateCommandBuffers failed ({})", static_cast<int>(result));
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    result = vkBeginCommandBuffer(cmd, &beginInfo);
    if (result != VK_SUCCESS)
    {
        Log.Error("upload: vkBeginCommandBuffer failed ({})", static_cast<int>(result));
        vkFreeCommandBuffers(Device, UploadPool, 1, &cmd);
        return VK_NULL_HANDLE;
    }

    return cmd;
}

bool VulkanUploadContextService::Submit(VkCommandBuffer cmd)
{
    if (cmd == VK_NULL_HANDLE) return false;

    VkResult result = vkEndCommandBuffer(cmd);
    if (result != VK_SUCCESS)
    {
        Log.Error("upload: vkEndCommandBuffer failed ({})", static_cast<int>(result));
        vkFreeCommandBuffers(Device, UploadPool, 1, &cmd);
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    vkResetFences(Device, 1, &UploadFence);
    result = vkQueueSubmit(UploadQueue, 1, &submit, UploadFence);
    if (result != VK_SUCCESS)
    {
        Log.Error("upload: vkQueueSubmit failed ({})", static_cast<int>(result));
        vkFreeCommandBuffers(Device, UploadPool, 1, &cmd);
        return false;
    }

    vkWaitForFences(Device, 1, &UploadFence, VK_TRUE, ULLONG_MAX);
    vkFreeCommandBuffers(Device, UploadPool, 1, &cmd);
    return true;
}
