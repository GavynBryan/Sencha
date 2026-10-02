#pragma once

#include <math/Vec.h>

#include <filesystem>
#include <span>
#include <string>

// Loads a theme JSON over the built-in EditorUi palette:
//
//   { "colors": { "accent": "#00E5CC", "window_bg": "#0A0D0FFF", ... } }
//
// Keys are the snake_case palette names (the table in the .cpp); values are
// sRGB hex, #RRGGBB or #RRGGBBAA, linearized on load -- the swapchain encodes on
// write, so the authored hex is what lands on screen. Missing keys keep the
// built-in default; unknown keys and bad values warn through *error but do not
// fail. The retired ImGui chrome's "metrics", "decor" and "surfaces" sections
// are read past.
[[nodiscard]] bool LoadEditorTheme(const std::filesystem::path& path, std::string* error);

// One "#RRGGBB"/"#RRGGBBAA" sRGB hex as a linear color; false when malformed.
[[nodiscard]] bool ParseThemeColor(const std::string& hex, float& r, float& g, float& b, float& a);

// The inverse: a linear palette color as the authored sRGB hex, always eight
// digits, so a theme and the stylesheet generated from it cannot disagree.
[[nodiscard]] std::string ThemeColorHex(const Vec4& linear);

// One themeable palette entry: the JSON key and the EditorUi color it drives.
struct EditorThemePaletteEntry
{
    const char* Key;
    Vec4* Color;
};

[[nodiscard]] std::span<const EditorThemePaletteEntry> EditorThemePalette();

// Restores every palette entry to the built-in default.
void ResetEditorTheme();
