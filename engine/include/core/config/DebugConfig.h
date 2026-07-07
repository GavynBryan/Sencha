#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <string>

struct EngineDebugConfig
{
    bool ConsoleLogging = true;
    bool DebugUi = false;
    // When set, logs also go to this file (truncated on start). PIE uses it to
    // hand the out-of-process player's log to the editor, which tails the file.
    std::string LogFilePath;
};

struct DebugConfigError
{
    std::string Message;
};

std::optional<EngineDebugConfig> DeserializeDebugConfig(
    const JsonValue& root,
    DebugConfigError* error = nullptr);
