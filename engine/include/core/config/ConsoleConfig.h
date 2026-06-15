#pragma once

#include <core/json/JsonValue.h>

#include <optional>
#include <string>
#include <vector>

struct EngineConsoleCVarAssignment
{
    std::string Name;
    std::string Value;
};

struct EngineConsoleConfig
{
    bool UiEnabled = true;
    bool OpenOnStart = false;
    int HistoryCapacity = 256;
    std::vector<EngineConsoleCVarAssignment> CVars;
    std::vector<std::string> ExecScripts;
};

struct ConsoleConfigError
{
    std::string Message;
};

std::optional<EngineConsoleConfig> DeserializeConsoleConfig(
    const JsonValue& root,
    ConsoleConfigError* error = nullptr);
