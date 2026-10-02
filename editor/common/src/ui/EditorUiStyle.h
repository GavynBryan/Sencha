#pragma once

#include <math/Vec.h>

#include <cmath>

// The editor's palette: the colors the authored UI's theme stylesheet and the
// views' overlays are drawn in, and the scale window-pixel sizes take.
namespace EditorUi
{
// Color-space note: the editor renders into an sRGB swapchain that encodes on
// write, so the palette is stored LINEAR; the hex below is the authored color.
namespace detail
{
inline float ToLinear(float s)
{
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}
// sRGB hex (0xRRGGBB) -> linear.
inline Vec4 Hex(unsigned int rgb, float a = 1.0f)
{
    return Vec4(ToLinear(((rgb >> 16) & 0xFF) / 255.0f), ToLinear(((rgb >> 8) & 0xFF) / 255.0f),
                ToLinear((rgb & 0xFF) / 255.0f), a);
}
}

// Palette: the workstation look. A near-black blue ground, gunmetal plates,
// cyan for anything interactive (hover, focus, the technology reading out),
// amber only for the selection and the important action, violet rarely and
// decoratively, red for the destructive. The authored UI's theme stylesheet is
// generated from these (AuthoredThemeStyleSheet).
// Mutable: EditorThemeFile overwrites entries from a theme JSON; these
// initializers are the built-in default, recorded as editor/themes/workstation.json.
inline Vec4 WindowBg       = detail::Hex(0x080B10);                 // #080B10 near-black blue ground
inline Vec4 PanelBg        = detail::Hex(0x0B0F15);                 // #0B0F15 panel well / popup
inline Vec4 HeaderBg       = detail::Hex(0x121821);                 // #121821 title / menu / tab rail
inline Vec4 FrameBg        = detail::Hex(0x0A0D12);                 // #0A0D12 inset wells (inputs)
inline Vec4 FrameBgHovered = detail::Hex(0x16202B);                 // #16202B
inline Vec4 FrameBgActive  = detail::Hex(0x0E3A44);                 // #0E3A44 cyan-dark, lit interior
inline Vec4 Border         = detail::Hex(0x24303C);                 // #24303C steel hairline
inline Vec4 Accent         = detail::Hex(0x2ED0EA);                 // #2ED0EA cyan
inline Vec4 AccentHover    = detail::Hex(0x7FE9FF);                 // #7FE9FF bright cyan
inline Vec4 AccentDim      = detail::Hex(0x0E3A44);                 // #0E3A44 cyan-dark
inline Vec4 Selected       = detail::Hex(0x4D330B);                 // #4D330B amber-dark selection fill
inline Vec4 Secondary      = detail::Hex(0x8B5CF6);                 // #8B5CF6 violet
inline Vec4 SecondaryHover = detail::Hex(0xA78BFA);                 // #A78BFA
inline Vec4 ButtonBg       = detail::Hex(0x131A23);                 // #131A23 mounted button face
inline Vec4 ButtonHovered  = detail::Hex(0x1B2633);                 // #1B2633
inline Vec4 Warning        = detail::Hex(0xEE9D26);                 // #EE9D26 amber (status, never selection)
inline Vec4 Danger         = detail::Hex(0xE0473C);                 // #E0473C red
inline Vec4 Critical       = detail::Hex(0xF0508C);                 // #F0508C magenta
inline Vec4 Success        = detail::Hex(0x2ED0EA);                 // cyan (use accent)
inline Vec4 TextPrimary    = detail::Hex(0xD6E4F0);                 // #D6E4F0 pale blue-white
inline Vec4 TextDim        = detail::Hex(0x6E8296);                 // #6E8296 desaturated blue-gray

// Workstation chassis layers, consumed by the chrome primitives: the darkest
// ground the whole shell sits on, the metal a frame is milled from, and the two
// bevel edges that make it read as raised. SelectedOutline is the bright form of
// Selected, for outlines around the thing currently being edited (the active
// viewport, the active material tile, the inspected entity's title); Selected
// itself is the fill behind a selected row.
inline Vec4 ChassisBg       = detail::Hex(0x05070A);                // #05070A
inline Vec4 MetalBase       = detail::Hex(0x1A222C);                // #1A222C graphite
inline Vec4 MetalHighlight  = detail::Hex(0x3A4856);                // #3A4856 steel-blue
inline Vec4 MetalShadow     = detail::Hex(0x03050A);                // #03050A
inline Vec4 SelectedOutline = detail::Hex(0xFFB347);                // #FFB347 bright amber
// The mounted control under the cursor: a yellow apart from both the cyan of
// focus and the amber of selection.
inline Vec4 ControlHover    = detail::Hex(0xFFD84A);                // #FFD84A yellow

// Palette-color math (toward white / toward black, alpha preserved or replaced),
// on the linear values directly.
inline Vec4 Lighten(const Vec4& c, float amount)
{
    return Vec4(c.X + (1.0f - c.X) * amount, c.Y + (1.0f - c.Y) * amount,
                  c.Z + (1.0f - c.Z) * amount, c.W);
}
inline Vec4 Darken(const Vec4& c, float amount)
{
    return Vec4(c.X * (1.0f - amount), c.Y * (1.0f - amount), c.Z * (1.0f - amount), c.W);
}
// Blends toward `other`; t = 0 keeps `c`. For deriving a color that belongs to
// two palette roles at once rather than inventing a literal for it.
inline Vec4 Mix(const Vec4& c, const Vec4& other, float t)
{
    return Vec4(c.X + (other.X - c.X) * t, c.Y + (other.Y - c.Y) * t,
                  c.Z + (other.Z - c.Z) * t, c.W);
}

inline Vec4 WithAlpha(const Vec4& c, float alpha)
{
    return Vec4(c.X, c.Y, c.Z, alpha);
}

// UI scale for sizes drawn in window pixels outside the authored UI (view
// handles, dials): the primary window's, from the editor.ui.scale cvar or its
// display (UiScaleChoice.h). Design pixels pass through Px() once, where drawn.
inline float UiScale = 1.0f;
inline float Px(float designPx)
{
    return designPx * UiScale;
}
} // namespace EditorUi
