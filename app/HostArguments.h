#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

//=============================================================================
// The runtime host's own command line, separated from the console's.
//
// `app` owns a handful of flags that decide what kind of process this is; the
// rest of argv is the engine's startup script (+map, +set, ...). Every flag is
// extracted before any is applied, so `--headless --settings x` and
// `--settings x --headless` are the same launch, and the order two flags were
// typed in never decides a posture.
//
// Pure: no SDL, no filesystem, no environment. The platform facts it needs --
// where a user's configuration directory is -- are handed in, which is what
// lets the precedence be tested as a function.
//=============================================================================
struct HostArguments
{
    // --headless: no window, no graphics, no player, and no application shell.
    bool Headless = false;
    // --game <path>
    std::optional<std::string> GamePath;
    // --content-root <dir>, repeatable, in order.
    std::vector<std::string> ContentRoots;
    // --settings <dir|none>. Unset means the default for this posture.
    std::optional<std::string> Settings;
};

// Strips the host's flags out of argv (compacting it in place, argv[0] kept)
// and reports them. A flag that wants a value and has none is left for the
// engine to reject as an unknown startup command rather than silently eaten.
[[nodiscard]] HostArguments ParseHostArguments(int& argc, char** argv);

// The directory saved settings live under for this launch, or empty for none.
// Precedence, highest first:
//   --settings <dir>  >  --settings none  >  headless (none)  >  platformRoot.
// A headless host persists nothing by default: a server's frame cap and window
// mode are the operator's, decided per launch, and a value saved by whoever
// last played on the machine has no business overriding them.
[[nodiscard]] std::string ResolveSettingsRoot(const HostArguments& arguments,
                                              std::string_view platformRoot);
