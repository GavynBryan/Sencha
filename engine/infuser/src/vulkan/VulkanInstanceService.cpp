#include <vulkan/VulkanInstanceService.h>
#include <vulkan/IVulkanSurfaceProvider.h>
#include <algorithm>
#include <cstring>

static constexpr const char* ValidationLayerName = "VK_LAYER_KHRONOS_validation";

VulkanInstanceService::VulkanInstanceService(Logger& logger,
                                             const CreateInfo& info,
                                             const IVulkanSurfaceProvider* surfaceProvider)
    : Log(logger)
{
    if (info.EnableValidation && !CheckValidationLayerSupport())
    {
        Log.Warn("Validation layers requested but not available. Continuing without them.");
    }
    else
    {
        ValidationEnabled = info.EnableValidation;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = info.AppName.c_str();
    appInfo.applicationVersion = info.AppVersion;
    appInfo.pEngineName        = "Sencha";
    appInfo.engineVersion      = VK_MAKE_API_VERSION(0, 1, 0, 0);
    appInfo.apiVersion         = info.ApiVersion;

    auto extensions = BuildExtensionList(info, surfaceProvider);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (ValidationEnabled)
    {
        createInfo.enabledLayerCount   = 1;
        createInfo.ppEnabledLayerNames = &ValidationLayerName;

        debugCreateInfo = MakeDebugCreateInfo(this);
        createInfo.pNext = &debugCreateInfo;
    }

    VkResult result = vkCreateInstance(&createInfo, nullptr, &Instance);
    if (result != VK_SUCCESS)
    {
        Log.Error("vkCreateInstance failed with code {}", static_cast<int>(result));
        Instance = VK_NULL_HANDLE;
        return;
    }

    Log.Info("Vulkan instance created successfully");

    if (ValidationEnabled)
    {
        SetupDebugMessenger();
    }
}

VulkanInstanceService::~VulkanInstanceService()
{
    if (DebugMessenger != VK_NULL_HANDLE)
    {
        DestroyDebugMessenger();
    }

    if (Instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(Instance, nullptr);
        Log.Info("Vulkan instance destroyed");
    }
}

bool VulkanInstanceService::CheckValidationLayerSupport() const
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> available(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, available.data());

    for (const auto& layer : available)
    {
        if (std::strcmp(layer.layerName, ValidationLayerName) == 0)
            return true;
    }

    return false;
}

std::vector<const char*> VulkanInstanceService::BuildExtensionList(
    const CreateInfo& info,
    const IVulkanSurfaceProvider* surfaceProvider) const
{
    std::vector<const char*> extensions;

    if (surfaceProvider)
    {
        auto surfaceExts = surfaceProvider->GetRequiredInstanceExtensions();
        extensions.insert(extensions.end(), surfaceExts.begin(), surfaceExts.end());
    }

    if (ValidationEnabled)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    extensions.insert(extensions.end(), info.ExtraExtensions.begin(), info.ExtraExtensions.end());

    Log.Info("Requesting {} instance extension(s):", extensions.size());
    for (const auto* ext : extensions)
    {
        Log.Info("  {}", ext);
    }

    return extensions;
}

void VulkanInstanceService::SetupDebugMessenger()
{
    auto createInfo = MakeDebugCreateInfo(this);

    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT"));

    if (!func)
    {
        Log.Warn("vkCreateDebugUtilsMessengerEXT not available");
        return;
    }

    VkResult result = func(Instance, &createInfo, nullptr, &DebugMessenger);
    if (result != VK_SUCCESS)
    {
        Log.Warn("Failed to set up debug messenger (code {})", static_cast<int>(result));
        DebugMessenger = VK_NULL_HANDLE;
    }
    else
    {
        Log.Info("Vulkan debug messenger created");
    }
}

void VulkanInstanceService::DestroyDebugMessenger()
{
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (func)
    {
        func(Instance, DebugMessenger, nullptr);
        Log.Info("Vulkan debug messenger destroyed");
    }

    DebugMessenger = VK_NULL_HANDLE;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanInstanceService::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData)
{
    auto* self = static_cast<VulkanInstanceService*>(userData);

    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        self->Log.Error("[Vulkan] {}", callbackData->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        self->Log.Warn("[Vulkan] {}", callbackData->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        self->Log.Info("[Vulkan] {}", callbackData->pMessage);
    else
        self->Log.Debug("[Vulkan] {}", callbackData->pMessage);

    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT VulkanInstanceService::MakeDebugCreateInfo(void* userData)
{
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = DebugCallback;
    info.pUserData       = userData;
    return info;
}
