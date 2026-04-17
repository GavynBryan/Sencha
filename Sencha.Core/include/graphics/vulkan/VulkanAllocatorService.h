#pragma once

#include <core/logging/LoggingProvider.h>
#include <core/service/IService.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

class VulkanDeviceService;
class VulkanInstanceService;
class VulkanPhysicalDeviceService;

//=============================================================================
// VulkanAllocatorService
//
// Owns the VmaAllocator that every downstream buffer/image service allocates
// against. Sits between the bootstrap substrate (instance/device) and the
// resource services; nothing below the allocator should ever call
// vkAllocateMemory directly.
//
// The allocator is a strictly serial resource: call sites that touch it
// during frame recording must do so from the thread that owns the command
// buffer. Creation and destruction happen at service lifetime boundaries,
// so no locking is needed at the service layer.
//=============================================================================
class VulkanAllocatorService : public IService
{
public:
    VulkanAllocatorService(LoggingProvider& logging,
                           VulkanInstanceService& instance,
                           VulkanPhysicalDeviceService& physicalDevice,
                           VulkanDeviceService& device);
    ~VulkanAllocatorService() override;

    VulkanAllocatorService(const VulkanAllocatorService&) = delete;
    VulkanAllocatorService& operator=(const VulkanAllocatorService&) = delete;
    VulkanAllocatorService(VulkanAllocatorService&&) = delete;
    VulkanAllocatorService& operator=(VulkanAllocatorService&&) = delete;

    [[nodiscard]] bool IsValid() const { return Allocator != VK_NULL_HANDLE; }
    [[nodiscard]] VmaAllocator GetAllocator() const { return Allocator; }

private:
    Logger& Log;
    VmaAllocator Allocator = VK_NULL_HANDLE;
};
