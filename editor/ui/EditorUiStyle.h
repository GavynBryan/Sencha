#pragma once

#include <imgui.h>

// The editor's UI palette and ImGui style — the single source for editor chrome
// colors and metrics (the 2D analog of EditorTheme.h for the 3D overlay). Panels
// reference these named constants; no panel hard-codes a color literal. Flat leaf
// data + Apply(): deliberately not a theming framework. (10-editor-ui-look-and-feel.md)
namespace EditorUi
{
// Palette — exact hex values supplied by the user (dark teal-black sci-fi with a
// glowing teal primary accent and a purple secondary). These are the single source
// of truth; the EditorUiSkin draw-list/texture layer and EditorUiStyle::Apply both
// pull from here. Hex -> ImVec4 (sRGB byte/255). (10-editor-ui-look-and-feel.md.)
inline const ImVec4 WindowBg       = ImVec4(0.0392f, 0.0510f, 0.0588f, 1.0f); // #0A0D0F
inline const ImVec4 PanelBg        = ImVec4(0.0392f, 0.0510f, 0.0588f, 1.0f); // #0A0D0F (window/child/popup)
inline const ImVec4 HeaderBg       = ImVec4(0.0588f, 0.1020f, 0.1098f, 1.0f); // #0F1A1C (title/menu)
inline const ImVec4 FrameBg        = ImVec4(0.0510f, 0.0706f, 0.0784f, 1.0f); // #0D1214 (inset wells)
inline const ImVec4 FrameBgHovered = ImVec4(0.1020f, 0.2078f, 0.2078f, 1.0f); // #1A3535
inline const ImVec4 FrameBgActive  = ImVec4(0.0000f, 0.2392f, 0.2078f, 1.0f); // #003D35
inline const ImVec4 Border         = ImVec4(0.1098f, 0.2275f, 0.2275f, 1.0f); // #1C3A3A
inline const ImVec4 Accent         = ImVec4(0.0000f, 0.8980f, 0.8000f, 1.0f); // #00E5CC teal
inline const ImVec4 AccentHover    = ImVec4(0.3000f, 1.0000f, 0.9200f, 1.0f); // brightened teal
inline const ImVec4 AccentDim      = ImVec4(0.0000f, 0.2392f, 0.2078f, 1.0f); // #003D35 teal-dark
inline const ImVec4 Selected       = ImVec4(0.0000f, 0.2392f, 0.2078f, 1.0f); // #003D35 selection
inline const ImVec4 Secondary      = ImVec4(0.4824f, 0.1843f, 0.7451f, 1.0f); // #7B2FBE purple
inline const ImVec4 SecondaryHover = ImVec4(0.6157f, 0.3725f, 0.8392f, 1.0f); // brightened purple
inline const ImVec4 ButtonBg       = ImVec4(0.0667f, 0.1255f, 0.1333f, 1.0f); // #112022
inline const ImVec4 ButtonHovered  = ImVec4(0.1020f, 0.2078f, 0.2078f, 1.0f); // #1A3535
inline const ImVec4 Warning        = ImVec4(0.9290f, 0.6160f, 0.1490f, 1.0f); // amber
inline const ImVec4 Danger         = ImVec4(0.8700f, 0.2550f, 0.2000f, 1.0f); // red
inline const ImVec4 Critical       = ImVec4(0.9570f, 0.1960f, 0.5530f, 1.0f); // magenta
inline const ImVec4 Success        = ImVec4(0.0000f, 0.8980f, 0.8000f, 1.0f); // teal (use accent)
inline const ImVec4 TextPrimary    = ImVec4(0.7843f, 0.9412f, 0.9255f, 1.0f); // #C8F0EC
inline const ImVec4 TextDim        = ImVec4(0.2275f, 0.3765f, 0.3765f, 1.0f); // #3A6060

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
