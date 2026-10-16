#pragma once

#include <core/logging/ILogSink.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <string_view>

// For tests that open a real window: they skip without one rather than fail.
[[nodiscard]] inline bool DisplayAvailable()
{
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return (display != nullptr && display[0] != '\0')
        || (wayland != nullptr && wayland[0] != '\0');
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
