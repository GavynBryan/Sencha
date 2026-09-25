#pragma once

#include <filesystem>

// Mounted after the game's roots, so a game may shadow engine content by path.
// SENCHA_ENGINE_CONTENT, else the installed layout beside the executable, else
// the source tree; empty when none exists.
[[nodiscard]] std::filesystem::path EngineContentRoot();
