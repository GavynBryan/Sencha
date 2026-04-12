#pragma once

#include <service/IService.h>
#include <logging/Logger.h>
#include <vulkan/VulkanBootstrapPolicy.h>
#include <vulkan/vulkan.h>

//=============================================================================
// VulkanInstanceService
//
// Owns VkInstance and the optional debug messenger. Created early in the
// engine lifetime and destroyed after all other Vulkan services.
//
// Construction:
//   auto& inst = services.AddService<VulkanInstanceService>(logger, info, &window);
//   if (!inst.IsValid()) { /* handle failure */ }
//
//=============================================================================
class VulkanInstanceService : public IService
{
public:
    VulkanInstanceService(Logger& logger, const VulkanBootstrapPolicy& policy);
    ~VulkanInstanceService() override;

    VulkanInstanceService(const VulkanInstanceService&) = delete;
    VulkanInstanceService& operator=(const VulkanInstanceService&) = delete;
    VulkanInstanceService(VulkanInstanceService&&) = delete;
    VulkanInstanceService& operator=(VulkanInstanceService&&) = delete;

    bool        IsValid() const { return Instance != VK_NULL_HANDLE; }
    VkInstance  GetInstance() const { return Instance; }
    bool        IsValidationEnabled() const { return ValidationEnabled; }

private:
    Logger& Log;

    VkInstance               Instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
    bool                     ValidationEnabled = false;

    bool CheckValidationLayerSupport() const;
    bool CheckInstanceExtensionSupport(const char* extension) const;
    std::vector<const char*> BuildExtensionList(const VulkanBootstrapPolicy& policy) const;
    void SetupDebugMessenger();
    void DestroyDebugMessenger();

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData);

    static VkDebugUtilsMessengerCreateInfoEXT MakeDebugCreateInfo(void* userData);
};
