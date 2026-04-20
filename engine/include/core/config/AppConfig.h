#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <string>

struct EngineAppConfig
{
    std::string Name = "Sencha Application";
};

struct AppConfigError
{
    std::string Message;
};

std::optional<EngineAppConfig> DeserializeAppConfig(
    const JsonValue& root,
    AppConfigError* error = nullptr);
