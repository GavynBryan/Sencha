#pragma once

#include <service/IService.h>
#include <logging/Logger.h>
#include <vulkan/vulkan.h>
#include <vulkan/VulkanQueueFamilies.h>
#include <vector>
#include <string>

class VulkanInstanceService;
class SdlWindow;

//=============================================================================
// VulkanDeviceService
//
// Owns VkDevice and the selected VkPhysicalDevice. Optionally creates a
// VkSurfaceKHR for an SDL window.
//
// Construction:
//   auto& dev = services.AddService<VulkanDeviceService>(
//       logger, instanceService, info, &window);
//   if (!dev.IsValid()) { /* handle failure */ }
//
// Queue retrieval:
//   VkQueue gfx = dev.GetGraphicsQueue();
//=============================================================================
class VulkanDeviceService : public IService
{
public:
    struct CreateInfo
    {
        std::vector<const char*> RequiredDeviceExtensions;
        bool                     PreferDiscreteGpu = true;
    };

    VulkanDeviceService(Logger& logger,
                        VulkanInstanceService& instance,
                        const CreateInfo& info,
                        const SdlWindow* window = nullptr);
    ~VulkanDeviceService() override;

    VulkanDeviceService(const VulkanDeviceService&) = delete;
    VulkanDeviceService& operator=(const VulkanDeviceService&) = delete;
    VulkanDeviceService(VulkanDeviceService&&) = delete;
    VulkanDeviceService& operator=(VulkanDeviceService&&) = delete;

    bool IsValid() const { return Device != VK_NULL_HANDLE; }

    VkPhysicalDevice            GetPhysicalDevice() const { return PhysicalDevice; }
    VkDevice                    GetDevice() const { return Device; }
    const VulkanQueueFamilies&  GetQueueFamilies() const { return QueueFamilies; }
    VkSurfaceKHR                GetSurface() const { return Surface; }

    VkQueue GetGraphicsQueue() const { return GraphicsQueue; }
    VkQueue GetPresentQueue() const { return PresentQueue; }
    VkQueue GetComputeQueue() const { return ComputeQueue; }
    VkQueue GetTransferQueue() const { return TransferQueue; }

private:
    Logger& Log;
    VkInstance Instance = VK_NULL_HANDLE;

    VkPhysicalDevice   PhysicalDevice = VK_NULL_HANDLE;
    VkDevice           Device         = VK_NULL_HANDLE;
    VkSurfaceKHR       Surface        = VK_NULL_HANDLE;
    VulkanQueueFamilies QueueFamilies;

    VkQueue GraphicsQueue = VK_NULL_HANDLE;
    VkQueue PresentQueue  = VK_NULL_HANDLE;
    VkQueue ComputeQueue  = VK_NULL_HANDLE;
    VkQueue TransferQueue = VK_NULL_HANDLE;

    bool PickPhysicalDevice(const CreateInfo& info);
    int  RateDevice(VkPhysicalDevice device, const CreateInfo& info) const;
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device, const CreateInfo& info) const;
    VulkanQueueFamilies FindQueueFamilies(VkPhysicalDevice device) const;
    bool CreateLogicalDevice(const CreateInfo& info);
    void RetrieveQueues();
    void LogDeviceProperties(VkPhysicalDevice device) const;
};
