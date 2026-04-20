#pragma once

#include <core/json/JsonValue.h>

#include <cstdint>
#include <optional>
#include <string>

struct EngineGraphicsConfig
{
    uint32_t FramesInFlight = 2;
    bool EnableValidation = true;
};

struct GraphicsConfigError
{
    std::string Message;
};

std::optional<EngineGraphicsConfig> DeserializeGraphicsConfig(
    const JsonValue& root,
    GraphicsConfigError* error = nullptr);
