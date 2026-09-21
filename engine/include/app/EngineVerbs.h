#pragma once

#include <authored/VerbRegistry.h>

#include <string_view>

//=============================================================================
// The engine's own authored vocabulary
//
// Names and argument contracts only. Declaring them installs no behaviour: a
// host with no application shell, and an editor with no game running at all,
// both install these so an author can see and validate the vocabulary, and both
// report the operations as unavailable until something binds them.
//
// This file names no service and includes no host type, which is what lets the
// runtime and every editor share one declaration rather than each keeping a
// list that nothing forces to agree.
//=============================================================================

// Leave the shell's pages and resume local play. Takes no arguments: what
// resuming does to the world is PauseState's answer, not a caller's choice.
inline constexpr std::string_view kRuntimeResumeVerb = "runtime.resume";

// Ask the host to exit. A request, not a termination -- a game's exit handler
// still gets to put a confirmation in front of it.
inline constexpr std::string_view kApplicationQuitVerb = "application.quit";

// Declares the above into one catalog, as the provider "engine". False means at
// least one declaration was refused, and the reasons are on the registry's
// installation errors.
[[nodiscard]] bool DeclareEngineVerbs(VerbRegistry& registry);
