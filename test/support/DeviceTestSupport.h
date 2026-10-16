#pragma once

#include <core/logging/ILogSink.h>
#include <SDL3/SDL.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <string_view>

// For tests that open a real window: they skip without one rather than fail.
[[nodiscard]] inline bool DisplayAvailable()
{
#if defined(_WIN32) || defined(__APPLE__)
    // A native desktop does not advertise itself through X11/Wayland variables.
    return true;
#else
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return (display != nullptr && display[0] != '\0')
        || (wayland != nullptr && wayland[0] != '\0');
#endif
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
