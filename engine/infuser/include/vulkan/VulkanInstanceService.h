#pragma once

#include <service/IService.h>
#include <logging/Logger.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

class IVulkanSurfaceProvider;

//=============================================================================
// VulkanInstanceService
//
// Owns VkInstance and the optional debug messenger. Created early in the
// engine lifetime and destroyed after all other Vulkan services.
//
// Construction:
//   auto& inst = services.AddService<VulkanInstanceService>(logger, provider);
//   if (!inst.IsValid()) { /* handle failure */ }
//
// The optional IVulkanSurfaceProvider* is used ONLY to query additional
// required instance extensions. It does not create a surface — that
// responsibility belongs to VulkanDeviceService.
//=============================================================================
class VulkanInstanceService : public IService
{
public:
    struct CreateInfo
    {
        std::string AppName       = "Sencha";
        uint32_t    AppVersion    = VK_MAKE_API_VERSION(0, 1, 0, 0);
        uint32_t    ApiVersion    = VK_API_VERSION_1_3;
        bool        EnableValidation = true;
        std::vector<const char*> ExtraExtensions;
    };

    VulkanInstanceService(Logger& logger,
                          const CreateInfo& info,
                          const IVulkanSurfaceProvider* surfaceProvider = nullptr);
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
    std::vector<const char*> BuildExtensionList(const CreateInfo& info,
                                                const IVulkanSurfaceProvider* surfaceProvider) const;
    void SetupDebugMessenger();
    void DestroyDebugMessenger();

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
        void* userData);

    static VkDebugUtilsMessengerCreateInfoEXT MakeDebugCreateInfo(void* userData);
};
