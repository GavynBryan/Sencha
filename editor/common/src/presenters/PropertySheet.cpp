#include "presenters/PropertySheet.h"

#include <charconv>
#include <format>

namespace
{
UiRowControl ControlFor(const PropertyDescriptor& descriptor)
{
    switch (descriptor.Kind)
    {
    case PropertyKind::Group:  return UiRowControl::Section;
    case PropertyKind::Choice: return UiRowControl::Choice;
    case PropertyKind::Toggle: return UiRowControl::Toggle;
    case PropertyKind::Asset:  return UiRowControl::Asset;
    case PropertyKind::Color:  return UiRowControl::Color;
    // A number with a range is a slider; without one it is typed.
    case PropertyKind::Number: return descriptor.Max >= descriptor.Min ? UiRowControl::Range : UiRowControl::Text;
    case PropertyKind::Text:
    case PropertyKind::Vector:
    case PropertyKind::Unsupported:
    default:                   return UiRowControl::Text;
    }
}

double NumberOf(std::string_view text)
{
    double value = 0.0;
    (void)std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}
} // namespace

std::vector<UiRow> PropertySheet::Present(const IPropertySource& source, bool everything)
{
    Rows.clear();
    std::vector<UiRow> rows;
    for (const PropertyNode& node : source.Nodes())
        Append(node, 0, 0, everything, rows);
    return rows;
}

std::vector<UiRow> PropertySheet::Present(std::span<const PropertySection> sections, bool everything,
                                          std::string_view only)
{
    Rows.clear();
    std::vector<UiRow> rows;
    for (std::size_t section = 0; section < sections.size(); ++section)
    {
        if (sections[section].Source == nullptr)
            continue;
        std::vector<PropertyNode> nodes = sections[section].Source->Nodes();
        if (!only.empty())
            std::erase_if(nodes, [only](const PropertyNode& node) { return node.Key != only; });
        if (nodes.empty())
            continue;
        if (!sections[section].Heading.empty())
        {
            UiRow heading{ sections[section].Heading, "", "" };
            heading.Control = UiRowControl::Section;
            rows.push_back(std::move(heading));
            Rows.push_back({ {}, {}, section });
        }
        for (const PropertyNode& node : nodes)
            Append(node, 0, section, everything, rows);
    }
    return rows;
}

void PropertySheet::Append(const PropertyNode& node, int depth, std::size_t section, bool everything,
                           std::vector<UiRow>& rows)
{
    const PropertyDescriptor& descriptor = node.Descriptor;
    if (descriptor.Advanced && !everything)
        return;

    UiRow row{ descriptor.Label, node.Value, descriptor.Tooltip };
    row.Key = node.Key;
    row.Depth = depth;
    row.Control = ControlFor(descriptor);
    row.Editable = !descriptor.ReadOnly && descriptor.Kind != PropertyKind::Group
                && descriptor.Kind != PropertyKind::Unsupported;
    row.Choices = descriptor.Choices;
    if (row.Control == UiRowControl::Range)
    {
        row.Number = NumberOf(node.Value);
        row.Min = descriptor.Min;
        row.Max = descriptor.Max;
        row.Step = descriptor.Step > 0.0 ? descriptor.Step : (descriptor.Max - descriptor.Min) / 100.0;
    }
    rows.push_back(std::move(row));
    Rows.push_back({ node.Key, node.Value, section });
    for (const PropertyVerb& verb : descriptor.Verbs)
    {
        UiRow action{ verb.Label, "", "" };
        action.Key = verb.Id;
        action.Depth = depth;
        action.Control = UiRowControl::Action;
        action.Choices = verb.Choices;
        rows.push_back(std::move(action));
        Rows.push_back({ node.Key, {}, section, verb.Id, verb.Choices });
    }

    for (const PropertyNode& child : node.Children)
        Append(child, depth + 1, section, everything, rows);
}

IPropertySource* PropertySheet::SourceOf(std::span<const PropertySection> sections, std::size_t index) const
{
    if (index >= Rows.size() || Rows[index].Section >= sections.size())
        return nullptr;
    return sections[Rows[index].Section].Source;
}

PropertyEditResult PropertySheet::Set(IPropertySource& source, std::size_t index, std::string_view value)
{
    if (index >= Rows.size() || Rows[index].Key.empty() || !Rows[index].Verb.empty())
        return PropertyEditResult::Gone;
    const Presented& row = Rows[index];
    if (value == row.Value)
        return PropertyEditResult::Unchanged;
    if (!source.BeginEdit(row.Key))
        return PropertyEditResult::Gone;
    return source.CommitEdit(row.Key, value);
}

PropertyEditResult PropertySheet::Set(std::span<const PropertySection> sections, std::size_t index,
                                      std::string_view value)
{
    IPropertySource* source = SourceOf(sections, index);
    return source != nullptr ? Set(*source, index, value) : PropertyEditResult::Gone;
}

namespace
{
std::string ReportedText(const UiValue& reported)
{
    switch (reported.Kind())
    {
    case UiValueKind::Float: return std::format("{}", reported.AsFloat());
    case UiValueKind::Int:   return std::format("{}", reported.AsInt());
    case UiValueKind::Bool:  return reported.AsBool() ? "true" : "false";
    default:                 return std::string(reported.AsString());
    }
}
} // namespace

PropertyEditResult PropertySheet::SetFromControl(IPropertySource& source, std::size_t index, const UiValue& reported)
{
    return Set(source, index, ReportedText(reported));
}

PropertyEditResult PropertySheet::SetFromControl(std::span<const PropertySection> sections, std::size_t index,
                                                 const UiValue& reported)
{
    return Set(sections, index, ReportedText(reported));
}

PropertyEditResult PropertySheet::Flip(IPropertySource& source, std::size_t index)
{
    if (index >= Rows.size())
        return PropertyEditResult::Gone;
    return Set(source, index, Rows[index].Value == "true" ? "false" : "true");
}

PropertyEditResult PropertySheet::Flip(std::span<const PropertySection> sections, std::size_t index)
{
    IPropertySource* source = SourceOf(sections, index);
    return source != nullptr ? Flip(*source, index) : PropertyEditResult::Gone;
}

PropertyEditResult PropertySheet::Act(IPropertySource& source, std::size_t index, std::string_view choice)
{
    if (index >= Rows.size() || Rows[index].Verb.empty())
        return PropertyEditResult::Gone;
    return source.Act(Rows[index].Key, Rows[index].Verb, choice);
}

PropertyEditResult PropertySheet::Act(std::span<const PropertySection> sections, std::size_t index,
                                      std::string_view choice)
{
    IPropertySource* source = SourceOf(sections, index);
    return source != nullptr ? Act(*source, index, choice) : PropertyEditResult::Gone;
}

std::vector<std::string> PropertySheet::ChoicesOf(std::size_t index) const
{
    return index < Rows.size() ? Rows[index].Choices : std::vector<std::string>{};
}

std::vector<std::string> PropertySheet::CandidatesOf(std::span<const PropertySection> sections,
                                                    std::size_t index) const
{
    const IPropertySource* source = SourceOf(sections, index);
    return source != nullptr ? source->Candidates(Rows[index].Key) : std::vector<std::string>{};
}

void PropertySheet::Hold(std::size_t index)
{
    Held = index < Rows.size() ? index : kNothingHeld;
}

PropertyEditResult PropertySheet::Release(IPropertySource& source, std::size_t index, std::string_view value)
{
    Held = kNothingHeld;
    return Set(source, index, value);
}

PropertyEditResult PropertySheet::Release(std::span<const PropertySection> sections, std::size_t index,
                                          std::string_view value)
{
    Held = kNothingHeld;
    return Set(sections, index, value);
}
