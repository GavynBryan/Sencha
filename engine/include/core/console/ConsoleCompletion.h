#pragma once

#include <string>
#include <string_view>
#include <vector>

class ConsoleRegistry;

// Context-aware completion for a console input line, built entirely on the
// registry's existing enumeration. Positional, with no per-command knowledge:
//   token 0 (the command word)   -> matching command names
//   token 1 (the first argument) -> matching cvar names
//   token 2+                     -> none
// Every console command that takes an argument takes a cvar name as its first,
// so position alone is enough and there is no command-identity branch to keep in
// sync. A trailing space means the next (empty-prefix) token is being started.
// Results are sorted (the registry sorts each list).
[[nodiscard]] std::vector<std::string> SuggestConsoleCompletions(
    const ConsoleRegistry& registry,
    std::string_view line);
