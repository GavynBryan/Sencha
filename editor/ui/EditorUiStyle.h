#pragma once

#include <imgui.h>

// The editor's UI palette and ImGui style — the single source for editor chrome
// colors and metrics (the 2D analog of EditorTheme.h for the 3D overlay). Panels
// reference these named constants; no panel hard-codes a color literal. Flat leaf
// data + Apply(): deliberately not a theming framework. (10-editor-ui-look-and-feel.md)
namespace EditorUi
{
// Palette. Tuned dark — a "dead CRT / cursed Winamp skin": near-black gunmetal
// chrome, muted teal accent (not a glowing cyan), amber + lime VU secondaries.
// Backgrounds sit at single-digit % lightness so the editor recedes and the
// viewport/overlay carries the color. (See 10-editor-ui-look-and-feel.md.)
inline const ImVec4 WindowBg       = ImVec4(0.024f, 0.031f, 0.039f, 1.0f); // app void, near black
inline const ImVec4 PanelBg        = ImVec4(0.039f, 0.051f, 0.059f, 1.0f); // panel gunmetal
inline const ImVec4 HeaderBg       = ImVec4(0.055f, 0.071f, 0.082f, 1.0f); // titles/menus, raised
inline const ImVec4 FrameBg        = ImVec4(0.016f, 0.024f, 0.031f, 1.0f); // inset wells, darkest
inline const ImVec4 FrameBgHovered = ImVec4(0.063f, 0.090f, 0.106f, 1.0f);
inline const ImVec4 FrameBgActive  = ImVec4(0.090f, 0.129f, 0.153f, 1.0f);
inline const ImVec4 Border         = ImVec4(0.110f, 0.149f, 0.169f, 1.0f); // hairline steel
inline const ImVec4 Accent         = ImVec4(0.137f, 0.604f, 0.671f, 1.0f); // muted teal
inline const ImVec4 AccentHover    = ImVec4(0.220f, 0.722f, 0.788f, 1.0f);
inline const ImVec4 AccentDim      = ImVec4(0.078f, 0.337f, 0.376f, 1.0f);
inline const ImVec4 Selected       = ImVec4(0.071f, 0.188f, 0.216f, 1.0f); // dim teal row
inline const ImVec4 Warning        = ImVec4(0.851f, 0.557f, 0.149f, 1.0f); // amber
inline const ImVec4 Danger         = ImVec4(0.792f, 0.275f, 0.235f, 1.0f); // LOCKED red
inline const ImVec4 Critical       = ImVec4(0.890f, 0.196f, 0.467f, 1.0f); // magenta
inline const ImVec4 Success        = ImVec4(0.420f, 0.745f, 0.290f, 1.0f); // VU lime
inline const ImVec4 TextPrimary    = ImVec4(0.722f, 0.769f, 0.792f, 1.0f);
inline const ImVec4 TextDim        = ImVec4(0.404f, 0.451f, 0.482f, 1.0f);

// Applies the palette + metrics onto the ImGui style (seeded from StyleColorsDark
// so no entry is left uninitialized).
void Apply(ImGuiStyle& style);

// Builds the editor font atlas from the bundled TTFs (editor/fonts): Inter as the
// default UI font with Font Awesome 6 Solid icon glyphs merged in, plus JetBrains
// Mono for the console. Call after the ImGui context exists and before the render
// backend uploads the atlas. If a font file is missing it falls back to ImGui's
// built-in font rather than failing, so a stripped checkout still runs.
void LoadFonts(ImGuiIO& io);

// The bundled monospace font (JetBrains Mono), or nullptr if it wasn't loaded —
// nullptr makes ImGui::PushFont fall back to the default. For the console etc.
ImFont* MonoFont();
} // namespace EditorUi
