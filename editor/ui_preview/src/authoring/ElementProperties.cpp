#include "authoring/ElementProperties.h"

#include "authoring/UiPreviewSession.h"
#include "properties/SettingProperties.h"

#include <array>
#include <format>
#include <string_view>

namespace
{
constexpr std::array<std::string_view, 9> kComputed{
    "display", "position", "width", "height", "font-family", "font-size", "pointer-events", "tab-index", "overflow",
};

std::string Size(const UiElementBox& box, float scale)
{
    return std::format("{:.0f} x {:.0f} px · {:.4g} x {:.4g} dp", box.Width, box.Height, box.Width / scale,
                       box.Height / scale);
}

bool HasAttribute(const UiElementInfo& element, std::string_view name)
{
    for (const auto& [key, value] : element.Attributes)
        if (key == name)
            return true;
    return false;
}

bool RaisesEvents(const UiElementInfo& element)
{
    for (const auto& [key, value] : element.Attributes)
        if (key.starts_with("data-event-") || key.starts_with("on"))
            return true;
    return false;
}
} // namespace

std::vector<PropertyNode> ElementProperties::Nodes() const
{
    const std::optional<UiElementInfo> element = Session.Service().DescribeElement(Element());
    if (!element)
        return {};
    UiService& ui = Session.Service();
    const float scale = Session.DisplayScale() > 0.0f ? Session.DisplayScale() : 1.0f;

    std::vector<PropertyNode> nodes;
    nodes.push_back(NoteProperty("depth", "Depth", std::to_string(element->Depth)));
    const UiElementBox& border = element->Boxes.Border;
    nodes.push_back(GroupProperty(
        "boxes", "Boxes",
        { NoteProperty("boxes.position", "Position",
                       std::format("{:.0f}, {:.0f} px · {:.4g}, {:.4g} dp", border.X, border.Y, border.X / scale,
                                   border.Y / scale)),
          NoteProperty("boxes.margin", "Margin", Size(element->Boxes.Margin, scale)),
          NoteProperty("boxes.border", "Border", Size(border, scale)),
          NoteProperty("boxes.padding", "Padding", Size(element->Boxes.Padding, scale)),
          NoteProperty("boxes.content", "Content", Size(element->Boxes.Content, scale)) }));
    PropertyNode computed = GroupProperty("computed", "Computed");
    for (const std::string_view property : kComputed)
        computed.Children.push_back(NoteProperty(std::format("computed.{}", property), std::string(property),
                                                 ui.ComputedProperty(element->Ref, property).value_or("-")));
    nodes.push_back(std::move(computed));
    if (std::vector<PropertyNode> notes = Notes(*element); !notes.empty())
        nodes.push_back(GroupProperty("notes", "Notes", std::move(notes)));
    if (!element->Attributes.empty())
    {
        PropertyNode attributes = GroupProperty("attributes", "Attributes");
        for (const auto& [key, value] : element->Attributes)
            attributes.Children.push_back(NoteProperty("attribute." + key, key, value));
        nodes.push_back(std::move(attributes));
    }
    return nodes;
}

std::vector<PropertyNode> ElementProperties::Notes(const UiElementInfo& element) const
{
    UiService& ui = Session.Service();
    std::vector<PropertyNode> notes;
    const std::optional<std::string> tabIndex = ui.ComputedProperty(element.Ref, "tab-index");
    const bool focusable = tabIndex && *tabIndex != "none";
    const bool activatable = RaisesEvents(element) || element.Tag == "button" || element.Tag == "input"
                             || element.Tag == "select" || HasAttribute(element, "data-value");
    if (activatable && !focusable)
        notes.push_back(NoteProperty("notes.focus", "Unreachable",
                                     "Activatable but not focusable: no tab-index, so a controller cannot reach it."));

    const std::optional<std::string> pointerEvents = ui.ComputedProperty(element.Ref, "pointer-events");
    const bool root = !element.Parent.IsValid() || element.Depth <= 1;
    if (root && pointerEvents && *pointerEvents != "none")
        notes.push_back(NoteProperty("notes.pointer", "Takes the pointer",
                                     std::format("A root with pointer-events: {} takes every pointer event on the "
                                                 "surface. A menu wants that; an overlay over a game does not.",
                                                 *pointerEvents)));
    return notes;
}
