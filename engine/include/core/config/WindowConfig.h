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
};

struct WindowConfigError
{
    std::string Message;
};

std::optional<EngineWindowConfig> DeserializeWindowConfig(
    const JsonValue& root,
    WindowConfigError* error = nullptr);
