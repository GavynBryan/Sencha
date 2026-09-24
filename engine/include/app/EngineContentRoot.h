#pragma once

#include <filesystem>

// The engine's own content root: the application shell's default documents,
// the face they draw with, and the engine's authored defaults such as the
// engine fact schema. Every runtime mounts it after the game's roots, as a
// fallback a game may shadow by path; a tool that resolves content the way the
// runtime does mounts it the same way. SENCHA_ENGINE_CONTENT overrides it;
// otherwise the installed layout beside the executable, then the source tree
// in an in-tree build. Empty when none of them exists.
[[nodiscard]] std::filesystem::path EngineContentRoot();
