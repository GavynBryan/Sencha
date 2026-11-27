#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class ConsoleService;
class Engine;
class SavedGames;

inline constexpr std::string_view kSaveWriteVerb = "save.write";
inline constexpr std::string_view kSaveRestoreVerb = "save.restore";

void DeclareSavedGameVerbs(VerbRegistrationScope& scope);
VerbAdmission InvokeSaveWrite(SavedGames& saves, const VerbInvocation& invocation);
VerbAdmission InvokeSaveRestore(SavedGames& saves, const VerbInvocation& invocation);

void RegisterSavedGameCommands(ConsoleService& console, Engine& engine);
