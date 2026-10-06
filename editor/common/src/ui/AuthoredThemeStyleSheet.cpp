#include "AuthoredThemeStyleSheet.h"

#include "EditorThemeFile.h"
#include "EditorUiStyle.h"

#include <string>

namespace
{
// The vocabulary authored documents style themselves through.
//
// Small on purpose, and named for the role rather than the colour, so a theme
// can move a role without every document following it. Adding one is a claim
// that a surface needs a distinction the editor's palette already draws and
// these rules do not -- which is a real reason, and rarer than it sounds.
struct Rule
{
    const char* Selector;
    // Built per call, because the palette entries these read are overwritten in
    // place when a theme loads.
    std::string (*Declarations)();
};

[[nodiscard]] std::string Hex(const Vec4& color)
{
    return ThemeColorHex(color);
}

[[nodiscard]] std::string Color(const Vec4& color)
{
    return "color: " + Hex(color) + ";";
}

[[nodiscard]] std::string Fill(const Vec4& color)
{
    return "background-color: " + Hex(color) + ";";
}

[[nodiscard]] std::string Edge(const Vec4& color)
{
    // border-color, never border: a width is geometry, and the document owns
    // its own. A theme that set widths would relayout documents on a switch.
    return "border-color: " + Hex(color) + ";";
}

// A vector shape's paint: `<sector>` takes these rather than a box's.
[[nodiscard]] std::string ShapeFill(const Vec4& color)
{
    return "fill-color: " + Hex(color) + ";";
}

[[nodiscard]] std::string ShapeStroke(const Vec4& color)
{
    return "stroke-color: " + Hex(color) + ";";
}

[[nodiscard]] std::string Tint(const Vec4& color)
{
    // White art takes the role's colour; the art's own shading survives.
    return "image-color: " + Hex(color) + ";";
}

constexpr Rule kRules[] = {
    { "body", [] { return Color(EditorUi::TextPrimary); } },

    // A panel, dialog or popover: the ground an authored surface sits on.
    { ".theme-surface", [] { return Fill(EditorUi::PanelBg) + Edge(EditorUi::Border); } },
    // The bar across the top of one, and the text in it.
    { ".theme-header", [] { return Fill(EditorUi::HeaderBg) + Edge(EditorUi::Border); } },
    { ".theme-title", [] { return Color(EditorUi::Accent); } },
    // The bar across the bottom: status, counts, the last thing that happened.
    { ".theme-footer", [] { return Fill(EditorUi::WindowBg) + Color(EditorUi::TextDim); } },

    // What a modal puts between itself and the editor. Alpha is part of the
    // colour, so a theme decides how much of the editor shows through.
    { ".theme-scrim", [] { return Fill(EditorUi::WithAlpha(EditorUi::ChassisBg, 0.63f)); } },

    // An editable well, and what focus does to it.
    { ".theme-field", [] {
         return Fill(EditorUi::FrameBg) + Edge(EditorUi::Border) + Color(EditorUi::TextPrimary);
     } },
    { ".theme-field:hover", [] { return Fill(EditorUi::FrameBgHovered); } },
    { ".theme-field:focus", [] { return Fill(EditorUi::FrameBgActive) + Edge(EditorUi::Accent); } },

    // The same inset ground without the hover and focus: a list, a scroll
    // surface, a read-out. Separate from a field because lighting a whole list
    // when the pointer crosses it says something untrue about what is clickable.
    { ".theme-well", [] { return Fill(EditorUi::FrameBg) + Edge(EditorUi::Border); } },
    // The edge around a view, and around the one being edited.
    { ".theme-frame", [] { return Edge(EditorUi::Border); } },
    { ".theme-frame.active", [] { return Edge(EditorUi::SelectedOutline); } },
    // What the pointer is over, and a boundary to keep clear of.
    { ".theme-frame.hover", [] { return Edge(EditorUi::Accent); } },
    { ".theme-frame.caution", [] { return Edge(EditorUi::Warning); } },
    // A point marked on a picture, and the one picked.
    { ".theme-dot", [] { return ShapeFill(EditorUi::Accent); } },
    { ".theme-dot.selected", [] { return ShapeFill(EditorUi::SelectedOutline); } },
    // A clock's playhead and the marks along it: cosmetic, gameplay, picked.
    { ".theme-playhead", [] { return Fill(EditorUi::SelectedOutline); } },
    { ".theme-mark", [] { return ShapeFill(EditorUi::TextDim); } },
    { ".theme-mark.gameplay", [] { return ShapeFill(EditorUi::Warning); } },
    { ".theme-mark.selected", [] {
         return ShapeFill(EditorUi::SelectedOutline) + ShapeStroke(EditorUi::TextPrimary);
     } },
    // A value plotted along a clock's lane.
    { ".theme-bar", [] { return Fill(EditorUi::Accent); } },
    // A stretch along a clock's lane, and the one picked.
    { ".theme-span", [] {
         return Fill(EditorUi::ButtonBg) + Edge(EditorUi::Border) + Color(EditorUi::TextPrimary);
     } },
    { ".theme-span.selected", [] { return Fill(EditorUi::Selected) + Edge(EditorUi::SelectedOutline); } },
    { ".theme-marquee", [] { return Fill(EditorUi::WithAlpha(EditorUi::Accent, 0.16f)) + Edge(EditorUi::AccentHover); } },

    // A bare clickable glyph -- a close cross, a disclosure arrow. No plate
    // behind it until the pointer is on it.
    { ".theme-icon", [] { return Color(EditorUi::TextDim); } },
    { ".theme-icon:hover",
      [] { return Color(EditorUi::TextPrimary) + Fill(EditorUi::ButtonHovered); } },

    { ".theme-button", [] {
         return Fill(EditorUi::ButtonBg) + Edge(EditorUi::Border) + Color(EditorUi::TextPrimary);
     } },
    { ".theme-button:hover", [] { return Fill(EditorUi::ButtonHovered) + Edge(EditorUi::Accent); } },
    { ".theme-button.disabled", [] { return Fill(EditorUi::WindowBg) + Color(EditorUi::TextDim); } },

    // A row in a list: the inspector's fields, a profile list, a result set.
    // Selection is amber and hover is not, which is the rule the shell follows.
    { ".theme-row:hover", [] { return Fill(EditorUi::FrameBgHovered); } },
    { ".theme-row.selected", [] { return Fill(EditorUi::Selected) + Color(EditorUi::TextPrimary); } },
    { ".theme-row.disabled", [] { return Color(EditorUi::Mix(EditorUi::TextDim, EditorUi::PanelBg, 0.25f)); } },

    // Text roles. Dim is secondary information; muted is a value shown but not
    // offered; accent is the thing being read out.
    { ".theme-dim", [] { return Color(EditorUi::TextDim); } },
    { ".theme-muted", [] { return Color(EditorUi::Mix(EditorUi::TextDim, EditorUi::PanelBg, 0.25f)); } },
    { ".theme-accent", [] { return Color(EditorUi::Accent); } },
    { ".theme-warning", [] { return Color(EditorUi::Warning); } },
    { ".theme-danger", [] { return Color(EditorUi::Danger); } },

    // The machine the editor is built as: the rail along the top, the plates
    // instruments are mounted on, the keys pressed on them, the readouts that
    // report and the lamps that signal. Colours only, like everything here.
    { ".theme-rail", [] { return Fill(EditorUi::Mix(EditorUi::MetalBase, EditorUi::ChassisBg, 0.45f)) + Edge(EditorUi::MetalHighlight); } },
    { ".theme-plate", [] { return Fill(EditorUi::MetalBase) + Edge(EditorUi::MetalHighlight); } },
    { ".theme-key", [] { return Fill(EditorUi::ButtonBg) + Edge(EditorUi::MetalHighlight) + Color(EditorUi::TextPrimary); } },
    { ".theme-key:hover", [] { return Fill(EditorUi::ButtonHovered) + Edge(EditorUi::ControlHover); } },
    { ".theme-key.lit", [] { return Fill(EditorUi::Selected) + Edge(EditorUi::SelectedOutline) + Color(EditorUi::SelectedOutline); } },
    { ".theme-key.disabled", [] { return Fill(EditorUi::WindowBg) + Edge(EditorUi::Border) + Color(EditorUi::TextDim); } },
    { ".theme-key.destructive", [] { return Color(EditorUi::Danger); } },
    { ".theme-readout", [] { return Fill(EditorUi::ChassisBg) + Edge(EditorUi::Border) + Color(EditorUi::Accent); } },
    { ".theme-art", [] { return Tint(EditorUi::MetalHighlight); } },

    // A radial menu, machined: the plate the petals are set into, the rim
    // closing a ring, the petal a release would choose, the in-effect mark, the
    // thin accent along the rim, and the recessed glyph wells. Its shapes are
    // `<sector>`s, painted by fill and stroke.
    { ".theme-wheel-plate", [] { return ShapeFill(EditorUi::MetalShadow); } },
    { ".theme-wheel-rim", [] { return ShapeFill(EditorUi::MetalBase); } },
    { ".theme-wheel-petal", [] { return ShapeFill(EditorUi::FrameBg); } },
    { ".theme-wheel-hot", [] { return ShapeFill(EditorUi::Selected) + ShapeStroke(EditorUi::SelectedOutline); } },
    { ".theme-wheel-mark", [] { return ShapeFill(EditorUi::SelectedOutline); } },
    { ".theme-wheel-detail", [] { return ShapeFill(EditorUi::Accent); } },
    { ".theme-wheel-hub", [] { return ShapeFill(EditorUi::ChassisBg); } },
    { ".theme-wheel-well", [] {
         return ShapeFill(EditorUi::ChassisBg) + ShapeStroke(EditorUi::MetalHighlight) + Color(EditorUi::Accent);
     } },
    { ".theme-wheel-well .icon", [] { return Tint(EditorUi::Accent); } },
    { ".theme-wheel-well.active", [] { return Color(EditorUi::SelectedOutline); } },
    { ".theme-wheel-well.active .icon", [] { return Tint(EditorUi::SelectedOutline); } },
    { ".theme-wheel-well.lit", [] {
         return ShapeFill(EditorUi::Darken(EditorUi::Selected, 0.35f)) + ShapeStroke(EditorUi::SelectedOutline)
                + Color(EditorUi::Lighten(EditorUi::SelectedOutline, 0.35f));
     } },
    { ".theme-wheel-well.lit .icon", [] { return Tint(EditorUi::Lighten(EditorUi::SelectedOutline, 0.35f)); } },
    // The world map: a dimmed ground over the views, zones as plates, the
    // ways between them as bars.
    { ".theme-map-ground", [] { return Fill(EditorUi::WithAlpha(EditorUi::ChassisBg, 0.92f)); } },
    { ".theme-map-zone", [] { return Fill(EditorUi::MetalBase) + Edge(EditorUi::MetalHighlight) + Color(EditorUi::TextPrimary); } },
    { ".theme-map-zone.closed", [] { return Fill(EditorUi::PanelBg) + Color(EditorUi::TextDim); } },
    { ".theme-map-zone.focus", [] { return Edge(EditorUi::SelectedOutline); } },
    { ".theme-map-zone.problem", [] { return Edge(EditorUi::Danger); } },
    { ".theme-map-zone .problems", [] { return Color(EditorUi::Warning); } },
    { ".theme-map-zone.hot", [] { return Fill(EditorUi::FrameBgActive) + Edge(EditorUi::Accent); } },
    { ".theme-map-link", [] { return Fill(EditorUi::MetalHighlight) + Color(EditorUi::MetalHighlight); } },
    { ".theme-map-link.oneway", [] { return Fill(EditorUi::Accent) + Color(EditorUi::Accent); } },
    { ".theme-map-link.warned", [] { return Fill(EditorUi::Warning) + Color(EditorUi::Warning); } },
    { ".theme-map-link.failed", [] { return Fill(EditorUi::Danger) + Color(EditorUi::Danger); } },
    { ".theme-map-camera", [] { return Fill(EditorUi::SelectedOutline); } },
    { ".theme-wheel-word", [] { return Color(EditorUi::TextPrimary); } },
    { ".theme-wheel-word.active", [] { return Color(EditorUi::SelectedOutline); } },
    { ".theme-wheel-word.lit", [] { return Color(EditorUi::Lighten(EditorUi::SelectedOutline, 0.35f)); } },
    { ".theme-wheel-readout", [] { return Color(EditorUi::TextDim); } },
    { ".theme-wheel-readout.first", [] { return Color(EditorUi::Accent); } },
    // One part of a readout that opens something: a seam between parts, lit
    // while its popover is open.
    { ".theme-segment", [] { return Edge(EditorUi::Border); } },
    { ".theme-segment:hover", [] { return Fill(EditorUi::FrameBgHovered); } },
    { ".theme-segment.lit", [] { return Fill(EditorUi::Selected) + Color(EditorUi::SelectedOutline); } },
    // A slider's and a drop-down's parts, which a document styles by element.
    { "slidertrack", [] { return Fill(EditorUi::FrameBg); } },
    { "sliderprogress", [] { return Fill(EditorUi::AccentDim); } },
    { "sliderbar", [] { return Fill(EditorUi::Accent); } },
    { "sliderbar:hover, sliderbar:active", [] { return Fill(EditorUi::AccentHover); } },
    { "selectbox", [] { return Fill(EditorUi::FrameBg) + Edge(EditorUi::Accent); } },
    { "selectbox option:hover", [] { return Fill(EditorUi::FrameBgHovered); } },
    { "selectbox option:checked", [] { return Fill(EditorUi::Selected) + Color(EditorUi::TextPrimary); } },
    { ".theme-lamp", [] { return Fill(EditorUi::Mix(EditorUi::MetalBase, EditorUi::ChassisBg, 0.5f)) + Edge(EditorUi::MetalShadow); } },
    { ".theme-lamp.on", [] { return Fill(EditorUi::Accent); } },
    { ".theme-lamp.lit", [] { return Fill(EditorUi::SelectedOutline); } },
    { ".theme-lamp.warn", [] { return Fill(EditorUi::Warning); } },
    { ".theme-lamp.fault", [] { return Fill(EditorUi::Danger); } },

    // Scrollbars are element types rather than classes, so every scrolling
    // authored surface gets these without asking.
    { "scrollbarvertical, scrollbarhorizontal", [] { return Fill(EditorUi::WindowBg); } },
    { "scrollbarvertical sliderbar, scrollbarhorizontal sliderbar",
      [] { return Fill(EditorUi::MetalBase); } },
    { "scrollbarvertical sliderbar:hover, scrollbarhorizontal sliderbar:hover",
      [] { return Fill(EditorUi::MetalHighlight); } },
};
} // namespace

std::string BuildAuthoredThemeStyleSheet()
{
    std::string out =
        "/* Generated from the active editor theme. Colours only: an authored\n"
        "   document owns its own structure, so nothing here sets a size, a\n"
        "   width or a position. Edit the theme, not this. */\n";
    for (const Rule& rule : kRules)
    {
        out += rule.Selector;
        out += " { ";
        out += rule.Declarations();
        out += " }\n";
    }
    return out;
}
