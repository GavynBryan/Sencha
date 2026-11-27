#pragma once

#include <core/console/ConsoleTypes.h>

#include <span>
#include <string>

class AuthoredQueryDispatcher;
class ConsoleService;
class Engine;
class VerbDispatcher;
class World;

// One World's catalogs and the dispatchers bound to it. docs/gameplay/authored-api.md, "From the console".
struct VocabularyConsoleTarget
{
    World& Entities;
    VerbDispatcher* Verbs = nullptr;
    const AuthoredQueryDispatcher* Queries = nullptr;
};

// `args` is "<verb> [key=value ...]".
[[nodiscard]] ConsoleResult RunVerbCommand(const VocabularyConsoleTarget& target, std::span<const std::string> args);
// `args` is "<query> [key=value ...]".
[[nodiscard]] ConsoleResult RunQueryCommand(const VocabularyConsoleTarget& target, std::span<const std::string> args);

// The verb and query commands, acting on the engine's World and dispatchers.
void RegisterVocabularyCommands(ConsoleService& console, Engine& engine);
