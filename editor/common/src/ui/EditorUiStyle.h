#pragma once

#include <imgui.h>

#include <cmath>
#include <string_view>

// The editor's UI look data and ImGui style — the single source for editor chrome
// colors, metrics, scale, and typography (the 2D analog of EditorTheme.h for the
// 3D overlay). Panels reference these named values; no panel hard-codes a color
// literal or a design pixel. Flat leaf data + Apply(): deliberately not a theming
// framework. (10-editor-ui-look-and-feel.md)
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

// Palette: the workstation look. A near-black blue ground, gunmetal plates,
// cyan for anything interactive (hover, focus, the technology reading out),
// amber only for the selection and the important action, violet rarely and
// decoratively, red for the destructive. Single source of truth; the chrome
// layer and EditorUiStyle::Apply both pull from here.
// Mutable: EditorThemeFile overwrites entries from a user theme JSON at
// startup (data-driven chrome); these initializers are the built-in default,
// recorded as editor/themes/workstation.json.
inline ImVec4 WindowBg       = detail::Hex(0x080B10);                 // #080B10 near-black blue ground
inline ImVec4 PanelBg        = detail::Hex(0x0B0F15);                 // #0B0F15 panel well / popup
inline ImVec4 HeaderBg       = detail::Hex(0x121821);                 // #121821 title / menu / tab rail
inline ImVec4 FrameBg        = detail::Hex(0x0A0D12);                 // #0A0D12 inset wells (inputs)
inline ImVec4 FrameBgHovered = detail::Hex(0x16202B);                 // #16202B
inline ImVec4 FrameBgActive  = detail::Hex(0x0E3A44);                 // #0E3A44 cyan-dark, lit interior
inline ImVec4 Border         = detail::Hex(0x24303C);                 // #24303C steel hairline
inline ImVec4 Accent         = detail::Hex(0x2ED0EA);                 // #2ED0EA cyan
inline ImVec4 AccentHover    = detail::Hex(0x7FE9FF);                 // #7FE9FF bright cyan
inline ImVec4 AccentDim      = detail::Hex(0x0E3A44);                 // #0E3A44 cyan-dark
inline ImVec4 Selected       = detail::Hex(0x4D330B);                 // #4D330B amber-dark selection fill
inline ImVec4 Secondary      = detail::Hex(0x8B5CF6);                 // #8B5CF6 violet
inline ImVec4 SecondaryHover = detail::Hex(0xA78BFA);                 // #A78BFA
inline ImVec4 ButtonBg       = detail::Hex(0x131A23);                 // #131A23 mounted button face
inline ImVec4 ButtonHovered  = detail::Hex(0x1B2633);                 // #1B2633
inline ImVec4 Warning        = detail::Hex(0xEE9D26);                 // #EE9D26 amber (status, never selection)
inline ImVec4 Danger         = detail::Hex(0xE0473C);                 // #E0473C red
inline ImVec4 Critical       = detail::Hex(0xF0508C);                 // #F0508C magenta
inline ImVec4 Success        = detail::Hex(0x2ED0EA);                 // cyan (use accent)
inline ImVec4 TextPrimary    = detail::Hex(0xD6E4F0);                 // #D6E4F0 pale blue-white
inline ImVec4 TextDim        = detail::Hex(0x6E8296);                 // #6E8296 desaturated blue-gray

// Workstation chassis layers, consumed by the chrome primitives: the darkest
// ground the whole shell sits on, the metal a frame is milled from, and the two
// bevel edges that make it read as raised. SelectedOutline is the bright form of
// Selected, for outlines around the thing currently being edited (the active
// viewport, the active material tile, the inspected entity's title); Selected
// itself is the fill behind a selected row.
inline ImVec4 ChassisBg       = detail::Hex(0x05070A);                // #05070A
inline ImVec4 MetalBase       = detail::Hex(0x1A222C);                // #1A222C graphite
inline ImVec4 MetalHighlight  = detail::Hex(0x3A4856);                // #3A4856 steel-blue
inline ImVec4 MetalShadow     = detail::Hex(0x03050A);                // #03050A
inline ImVec4 SelectedOutline = detail::Hex(0xFFB347);                // #FFB347 bright amber

// Palette-color math (toward white / toward black, alpha preserved or replaced).
// Operates on the linear values directly, which is what the draw lists want.
inline ImVec4 Lighten(const ImVec4& c, float amount)
{
    return ImVec4(c.x + (1.0f - c.x) * amount, c.y + (1.0f - c.y) * amount,
                  c.z + (1.0f - c.z) * amount, c.w);
}
inline ImVec4 Darken(const ImVec4& c, float amount)
{
    return ImVec4(c.x * (1.0f - amount), c.y * (1.0f - amount), c.z * (1.0f - amount), c.w);
}
inline ImVec4 WithAlpha(const ImVec4& c, float alpha)
{
    return ImVec4(c.x, c.y, c.z, alpha);
}

// UI scale: one factor every on-screen size derives from. Resolved once at ImGui
// init (the editor.ui.scale cvar, or the window's display scale when it is 0)
// before Apply() and LoadFonts() run; changing it later needs a restart, since
// the font atlas is built at that size. Chrome metrics are authored in design
// pixels and pass through Px() exactly once, at the point they are drawn.
inline float UiScale = 1.0f;
inline float Px(float designPx)
{
    return designPx * UiScale;
}

// Chrome metrics, in design pixels (unscaled, so a theme file is display
// independent). Themed by EditorThemeFile's "metrics" object the same way the
// palette is themed by "colors". Only the chrome primitives read these.
struct ChromeMetrics
{
    float Chamfer = 6.0f;             // clipped-corner size of a Standard panel frame
    float Border = 1.0f;              // steel edge line width
    float Recess = 2.0f;              // depth of the content well inside a frame
    float RailHeight = 6.0f;          // header rail under a docked panel's tab
    float HeaderHeight = 20.0f;       // full titled header row
    float EdgeWidth = 1.0f;           // bevel highlight/shadow line width
    float GlowAlpha = 0.35f;          // focused-edge glow opacity, 0..1
    float GlowWidth = 3.0f;           // focused-edge glow spread
    float Tracking = 1.5f;            // letter spacing of the uppercase label roles
    float OrnamentMediumMin = 160.0f; // smallest panel dimension for the medium ornament tier
    float OrnamentLargeMin = 320.0f;  // smallest panel dimension for the large ornament tier
    float ModulePad = 3.0f;           // toolbar module recess around its controls
    float ScrewRadius = 3.0f;
    float VentLength = 24.0f;
    float StripeLength = 32.0f;
    float ChassisBorder = 4.0f;       // application chassis frame width
    float ChassisChamfer = 10.0f;
};
inline ChromeMetrics Metrics{};

// Applies the palette + metrics onto the ImGui style (seeded from a fresh
// ImGuiStyle + StyleColorsDark so no entry is left uninitialized), then scales
// every ImGui size by UiScale. Safe to call again after a theme change: it never
// compounds the scale.
void Apply(ImGuiStyle& style);

// Builds the editor font atlas from the bundled TTFs (editor/fonts): JetBrains
// Mono at the body size with Font Awesome 6 Solid icon glyphs merged in, plus
// the small and large faces the text roles use and the console's mono face.
// Sizes are multiplied by UiScale. Call after the ImGui context exists and before
// the render backend uploads the atlas. If a font file is missing it falls back
// to ImGui's built-in font rather than failing, so a stripped checkout still runs.
void LoadFonts(ImGuiIO& io);

// The bundled monospace font (JetBrains Mono), or nullptr if it wasn't loaded —
// nullptr makes ImGui::PushFont fall back to the default. For the console etc.
ImFont* MonoFont();

// Typography roles. A role names what a piece of text is; StyleFor turns that
// into a face, a color, and the uppercase + tracking treatment that separates the
// label roles from body copy while the editor ships one family. Body text stays
// plain ImGui; only the label roles go through DrawRoleText.
enum class TextRole
{
    ApplicationTitle, // the shell's product nameplate
    PanelTitle,       // a panel's name on its header
    SectionTitle,     // a labelled rule inside a panel
    Body,             // ordinary widget text
    Data,             // numeric readouts, console output (monospace)
    SecondaryText,    // supporting text, de-emphasized
    Status,           // status bar readouts and telemetry
};

struct TextStyle
{
    ImFont* Font = nullptr; // nullptr = the current default font
    ImVec4 Color{};
    bool Uppercase = false;
    float Tracking = 0.0f;  // extra advance per glyph, screen pixels (already scaled)
};

TextStyle StyleFor(TextRole role);

// Size of `text` drawn in `role` (uppercase + tracking applied), screen pixels.
ImVec2 MeasureRoleText(TextRole role, std::string_view text);

// Draws `text` in `role` at `pos` into `dl`. colorOverride (a packed ImU32, 0 =
// none) replaces the role's color, for state tints such as a focused header.
void DrawRoleText(ImDrawList* dl, ImVec2 pos, TextRole role, std::string_view text, ImU32 colorOverride = 0);
} // namespace EditorUi
