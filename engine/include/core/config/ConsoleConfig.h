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

    // The directory the console's saved-settings archive lives under, or empty
    // for no archive at all. A desktop host resolves its platform's
    // configuration directory; a tool, a test or a server leaves it empty and
    // can never write a user's disk. The file inside is named by App.Name --
    // see CVarArchive::FileFor.
    std::string SettingsRoot;

    // A file descriptor to read console commands from, or -1 for none. A
    // dedicated host is administered through its terminal and the process host
    // points this at standard input; anything else leaves it closed. Naming a
    // descriptor rather than assuming one is what keeps two engines in one
    // process from each swallowing half of what someone typed.
    int CommandFd = -1;
};

struct ConsoleConfigError
{
    std::string Message;
};

std::optional<EngineConsoleConfig> DeserializeConsoleConfig(
    const JsonValue& root,
    ConsoleConfigError* error = nullptr);
