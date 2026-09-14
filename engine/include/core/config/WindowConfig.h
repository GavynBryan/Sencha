#pragma once

#include <core/json/JsonValue.h>
#include <platform/WindowTypes.h>

#include <cstdint>
#include <optional>
#include <string>

struct EngineWindowConfig
{
    std::string Title = "Sencha";
    uint32_t Width = 1280;
    uint32_t Height = 720;
    WindowMode Mode = WindowMode::Windowed;
    WindowGraphicsApi GraphicsApi = WindowGraphicsApi::Vulkan;
    bool Resizable = true;
    bool Visible = true;
    // Ask for a window without the platform's frame, decorated by the
    // application (its own caption, drag, and resize regions). A request: the
    // window falls back to the platform frame where hit testing is
    // unavailable, and reports what it actually has.
    bool ClientDecorations = false;
};

struct WindowConfigError
{
    std::string Message;
};

std::optional<EngineWindowConfig> DeserializeWindowConfig(
    const JsonValue& root,
    WindowConfigError* error = nullptr);
