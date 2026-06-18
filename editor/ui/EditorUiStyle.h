#pragma once

#include <imgui.h>

// The editor's UI palette and ImGui style — the single source for editor chrome
// colors and metrics (the 2D analog of EditorTheme.h for the 3D overlay). Panels
// reference these named constants; no panel hard-codes a color literal. Flat leaf
// data + Apply(): deliberately not a theming framework. (10-editor-ui-look-and-feel.md)
namespace EditorUi
{
// Palette — an "edgy 2003 Winamp skin": near-black blue-gunmetal chrome with a
// glowing cyan accent and saturated VU secondaries (green/amber/magenta). The
// EditorUiSkin draw-list layer turns these into glossy beveled gradients; the raw
// colors stay vivid so the highlights actually glow. (10-editor-ui-look-and-feel.md.)
inline const ImVec4 WindowBg       = ImVec4(0.012f, 0.016f, 0.022f, 1.0f); // app void, near black
inline const ImVec4 PanelBg        = ImVec4(0.031f, 0.043f, 0.055f, 1.0f); // panel gunmetal
inline const ImVec4 HeaderBg       = ImVec4(0.047f, 0.063f, 0.078f, 1.0f); // titles/menus, raised
inline const ImVec4 FrameBg        = ImVec4(0.008f, 0.014f, 0.020f, 1.0f); // inset wells, darkest
inline const ImVec4 FrameBgHovered = ImVec4(0.067f, 0.098f, 0.118f, 1.0f);
inline const ImVec4 FrameBgActive  = ImVec4(0.094f, 0.137f, 0.165f, 1.0f);
inline const ImVec4 Border         = ImVec4(0.133f, 0.180f, 0.204f, 1.0f); // hairline steel
inline const ImVec4 Accent         = ImVec4(0.168f, 0.745f, 0.847f, 1.0f); // glow cyan
inline const ImVec4 AccentHover    = ImVec4(0.357f, 0.910f, 1.000f, 1.0f); // hot cyan
inline const ImVec4 AccentDim      = ImVec4(0.094f, 0.408f, 0.467f, 1.0f);
inline const ImVec4 Selected       = ImVec4(0.082f, 0.220f, 0.255f, 1.0f); // dim teal row
inline const ImVec4 Warning        = ImVec4(0.929f, 0.616f, 0.149f, 1.0f); // amber
inline const ImVec4 Danger         = ImVec4(0.870f, 0.255f, 0.200f, 1.0f); // LOCKED red
inline const ImVec4 Critical       = ImVec4(0.957f, 0.196f, 0.553f, 1.0f); // magenta
inline const ImVec4 Success        = ImVec4(0.455f, 0.855f, 0.290f, 1.0f); // VU lime
inline const ImVec4 TextPrimary    = ImVec4(0.769f, 0.820f, 0.851f, 1.0f);
inline const ImVec4 TextDim        = ImVec4(0.435f, 0.486f, 0.522f, 1.0f);

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
