#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <string>

struct EngineDebugConfig
{
    bool ConsoleLogging = true;
    bool DebugUi = false;
};

struct DebugConfigError
{
    std::string Message;
};

std::optional<EngineDebugConfig> DeserializeDebugConfig(
    const JsonValue& root,
    DebugConfigError* error = nullptr);
