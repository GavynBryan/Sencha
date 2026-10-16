#pragma once

#include <core/logging/ILogSink.h>
#include <SDL3/SDL.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string_view>

// For real-device tests: skip only when the host has no display or no Vulkan
// physical device. A failed renderer init on an available device is still a test failure.
[[nodiscard]] inline bool DisplayAvailable()
{
#if !defined(_WIN32) && !defined(__APPLE__)
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if ((display == nullptr || display[0] == '\0')
        && (wayland == nullptr || wayland[0] == '\0'))
        return false;
#endif
    static const bool hasDevice = [] {
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo create{};
        create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create.pApplicationInfo = &app;
        VkInstance instance = VK_NULL_HANDLE;
        if (vkCreateInstance(&create, nullptr, &instance) != VK_SUCCESS)
            return false;
        std::uint32_t count = 0;
        const VkResult enumerated = vkEnumeratePhysicalDevices(instance, &count, nullptr);
        vkDestroyInstance(instance, nullptr);
        return enumerated == VK_SUCCESS && count != 0;
    }();
    return hasDevice;
}

inline bool SetImmediatePresentationForTest()
{
    return SDL_setenv_unsafe("SENCHA_PRESENT_MODE", "IMMEDIATE", 1) == 0;
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
