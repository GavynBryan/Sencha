#pragma once

#include <core/logging/ILogSink.h>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdlib>
#include <string_view>

[[nodiscard]] inline bool VulkanDeviceAvailable()
{
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&create, nullptr, &instance) != VK_SUCCESS)
        return false;
    uint32_t devices = 0;
    const VkResult result = vkEnumeratePhysicalDevices(instance, &devices, nullptr);
    vkDestroyInstance(instance, nullptr);
    return result == VK_SUCCESS && devices > 0;
}

// For tests that open a real window: they skip without a display or a Vulkan
// device rather than fail.
[[nodiscard]] inline bool DisplayAvailable()
{
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const bool hasDisplay = (display != nullptr && display[0] != '\0')
        || (wayland != nullptr && wayland[0] != '\0');
    return hasDisplay && VulkanDeviceAvailable();
}

// Fails the running test on every Vulkan validation error, and counts them.
class ValidationErrorCount : public ILogSink
{
public:
    explicit ValidationErrorCount(std::atomic<int>& count) : Count(count) {}

    void Write(LogLevel level, std::string_view, std::string_view message) override
    {
        if (level >= LogLevel::Error && message.starts_with("[Vulkan]"))
        {
            ++Count;
            ADD_FAILURE() << message;
        }
    }

private:
    std::atomic<int>& Count;
};
