#pragma once

#include <imgui.h>

#include <cmath>

// The editor's UI palette and ImGui style — the single source for editor chrome
// colors and metrics (the 2D analog of EditorTheme.h for the 3D overlay). Panels
// reference these named constants; no panel hard-codes a color literal. Flat leaf
// data + Apply(): deliberately not a theming framework. (10-editor-ui-look-and-feel.md)
namespace EditorUi
{
// Color-space note: the editor renders into an sRGB swapchain (the GPU encodes
// linear->sRGB on write, shared with the correct 3D path). ImGui authors colors in
// sRGB and would otherwise come out washed-out, so the palette is stored in LINEAR
// space: after the framebuffer's sRGB encode it lands back on the authored hex. The
// hex below is the authored/displayed color; Hex()/Srgb() linearize it once here so
// both EditorUiStyle::Apply and every draw-list call get correct values for free.
namespace detail
{
inline float ToLinear(float s)
{
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}
// sRGB hex (0xRRGGBB) -> linear ImVec4.
inline ImVec4 Hex(unsigned int rgb, float a = 1.0f)
{
    return ImVec4(ToLinear(((rgb >> 16) & 0xFF) / 255.0f),
                  ToLinear(((rgb >> 8) & 0xFF) / 255.0f),
                  ToLinear((rgb & 0xFF) / 255.0f), a);
}
// sRGB 0-1 floats -> linear ImVec4 (for colors authored as floats, not hex).
inline ImVec4 Srgb(float r, float g, float b, float a = 1.0f)
{
    return ImVec4(ToLinear(r), ToLinear(g), ToLinear(b), a);
}
}

// Palette — exact hex values supplied by the user (dark teal-black sci-fi with a
// glowing teal primary accent and a purple secondary). Single source of truth; the
// EditorUiSkin draw-list/texture layer and EditorUiStyle::Apply both pull from here.
inline const ImVec4 WindowBg       = detail::Hex(0x0A0D0F);                 // #0A0D0F
inline const ImVec4 PanelBg        = detail::Hex(0x0A0D0F);                 // #0A0D0F (window/child/popup)
inline const ImVec4 HeaderBg       = detail::Hex(0x0F1A1C);                 // #0F1A1C (title/menu)
inline const ImVec4 FrameBg        = detail::Hex(0x0D1214);                 // #0D1214 (inset wells)
inline const ImVec4 FrameBgHovered = detail::Hex(0x1A3535);                 // #1A3535
inline const ImVec4 FrameBgActive  = detail::Hex(0x003D35);                 // #003D35
inline const ImVec4 Border         = detail::Hex(0x1C3A3A);                 // #1C3A3A
inline const ImVec4 Accent         = detail::Hex(0x00E5CC);                 // #00E5CC teal
inline const ImVec4 AccentHover    = detail::Srgb(0.3000f, 1.0000f, 0.9200f); // brightened teal
inline const ImVec4 AccentDim      = detail::Hex(0x003D35);                 // #003D35 teal-dark
inline const ImVec4 Selected       = detail::Hex(0x003D35);                 // #003D35 selection
inline const ImVec4 Secondary      = detail::Hex(0x7B2FBE);                 // #7B2FBE purple
inline const ImVec4 SecondaryHover = detail::Srgb(0.6157f, 0.3725f, 0.8392f); // brightened purple
inline const ImVec4 ButtonBg       = detail::Hex(0x112022);                 // #112022
inline const ImVec4 ButtonHovered  = detail::Hex(0x1A3535);                 // #1A3535
inline const ImVec4 Warning        = detail::Srgb(0.9290f, 0.6160f, 0.1490f); // amber
inline const ImVec4 Danger         = detail::Srgb(0.8700f, 0.2550f, 0.2000f); // red
inline const ImVec4 Critical       = detail::Srgb(0.9570f, 0.1960f, 0.5530f); // magenta
inline const ImVec4 Success        = detail::Hex(0x00E5CC);                 // teal (use accent)
inline const ImVec4 TextPrimary    = detail::Hex(0xC8F0EC);                 // #C8F0EC
inline const ImVec4 TextDim        = detail::Hex(0x3A6060);                 // #3A6060

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
