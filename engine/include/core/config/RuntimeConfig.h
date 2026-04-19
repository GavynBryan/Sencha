#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <string>

struct EngineRuntimeConfig
{
    double FixedTickRate = 60.0;
    double TargetFps = 0.0;
    double ResizeSettleSeconds = 0.10;
    bool ExitOnEscape = false;
    bool TogglePauseOnF1 = false;
};

struct RuntimeConfigError
{
    std::string Message;
};

std::optional<EngineRuntimeConfig> DeserializeRuntimeConfig(
    const JsonValue& root,
    RuntimeConfigError* error = nullptr);
