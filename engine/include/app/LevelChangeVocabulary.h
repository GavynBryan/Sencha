#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class LevelChange;

inline constexpr std::string_view kLevelChangeVerb = "level.change";

void DeclareLevelChangeVerbs(VerbRegistrationScope& scope);
VerbAnswer InvokeLevelChange(LevelChange& changes, const VerbInvocation& invocation);
