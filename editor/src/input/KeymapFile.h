#pragma once

#include "InputEvent.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// User keybind overrides (data-driven keymap). A keybinds.json next to the
// editor's working files remaps actions by name:
//
//   { "edit.undo": "Ctrl+Z", "gizmo.move": "Shift+W", "mode.face": "4" }
//
// Actions are the names EditorServices registers its shortcut table under;
// chord text is modifiers + one SDL key name joined with '+'. Absent file or
// absent action = the built-in default binding.

struct KeyChord
{
    SDL_Keycode Key = SDLK_UNKNOWN;
    ModifierFlags Mods;
};

// "Ctrl+Shift+Z" -> chord. Case-insensitive modifiers (Ctrl/Shift/Alt); the
// final token resolves through SDL's key-name table. nullopt on anything
// malformed.
[[nodiscard]] std::optional<KeyChord> ParseKeyChord(std::string_view text);

// action name -> chord for every valid entry in the file. Missing file is not
// an error (empty map); malformed entries are reported through *error and
// skipped.
[[nodiscard]] std::unordered_map<std::string, KeyChord>
LoadKeymapOverrides(const std::filesystem::path& path, std::string* error);
