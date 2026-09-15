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

[[nodiscard]] std::string Hex(const ImVec4& color)
{
    return ThemeColorHex(color);
}

[[nodiscard]] std::string Color(const ImVec4& color)
{
    return "color: " + Hex(color) + ";";
}

[[nodiscard]] std::string Fill(const ImVec4& color)
{
    return "background-color: " + Hex(color) + ";";
}

[[nodiscard]] std::string Edge(const ImVec4& color)
{
    // border-color, never border: a width is geometry, and the document owns
    // its own. A theme that set widths would relayout documents on a switch.
    return "border-color: " + Hex(color) + ";";
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

    // Text roles. Dim is secondary information; muted is a value shown but not
    // offered; accent is the thing being read out.
    { ".theme-dim", [] { return Color(EditorUi::TextDim); } },
    { ".theme-muted", [] { return Color(EditorUi::Mix(EditorUi::TextDim, EditorUi::PanelBg, 0.25f)); } },
    { ".theme-accent", [] { return Color(EditorUi::Accent); } },
    { ".theme-warning", [] { return Color(EditorUi::Warning); } },
    { ".theme-danger", [] { return Color(EditorUi::Danger); } },

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
