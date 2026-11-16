#pragma once

#include <filesystem>

// The per-user configuration directory on this platform: $XDG_CONFIG_HOME,
// else $HOME/.config, else the working directory. Not SDL_GetPrefPath, which
// resolves the data home on Linux; ~/.config/sencha is already the product's
// convention for the launcher's own state, and settings belong beside it.
//
// A directory and not a file: what lives under it is decided by whoever owns
// the file -- the console names its archive, the launcher its catalog.
[[nodiscard]] std::filesystem::path UserConfigDirectory();

// Where a user's own data lives: $XDG_DATA_HOME, else $HOME/.local/share,
// else the working directory. Saves belong here, not beside settings.
[[nodiscard]] std::filesystem::path UserDataDirectory();
