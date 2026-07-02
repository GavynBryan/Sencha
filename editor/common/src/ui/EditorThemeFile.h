#pragma once

#include <imgui.h>

#include <filesystem>
#include <span>
#include <string>

// Loads a chrome theme JSON over the built-in EditorUi palette (data-driven
// chrome: retint the editor without a recompile). Format:
//
//   { "colors": { "accent": "#00E5CC", "window_bg": "#0A0D0FFF", ... } }
//
// Keys are the snake_case palette names (see the table in the .cpp); values are
// sRGB hex, #RRGGBB or #RRGGBBAA. Colors are AUTHORED in sRGB and linearized on
// load: the editor renders into an sRGB swapchain that encodes on write, so the
// authored hex is what lands on screen. Never brighten values to compensate.
// Missing keys keep the built-in default; unknown keys warn through *error but
// do not fail. Call before the ImGui style is applied.
[[nodiscard]] bool LoadEditorTheme(const std::filesystem::path& path, std::string* error);

// Parses one "#RRGGBB"/"#RRGGBBAA" sRGB hex into a linear color, false on a
// malformed string. Exposed for the theme tests.
[[nodiscard]] bool ParseThemeColor(const std::string& hex, float& r, float& g, float& b, float& a);

// One themeable palette entry: the JSON key and the EditorUi color it drives.
struct EditorThemePaletteEntry
{
    const char* Key;
    ImVec4* Color;
};

// The themeable palette in declaration order: the same table LoadEditorTheme maps
// keys through, exposed so the preferences UI can edit entries in place.
[[nodiscard]] std::span<const EditorThemePaletteEntry> EditorThemePalette();

// Restores every palette entry to the built-in default (the state before any
// theme load or override).
void ResetEditorThemePalette();

// Writes the current palette as a theme JSON, colors re-encoded to the authored
// sRGB hex form LoadEditorTheme reads back. False (with *error) on I/O failure.
[[nodiscard]] bool SaveEditorTheme(const std::filesystem::path& path, std::string* error);
