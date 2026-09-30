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
        Append(node, 0, everything, rows);
    return rows;
}

void PropertySheet::Append(const PropertyNode& node, int depth, bool everything, std::vector<UiRow>& rows)
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
    Rows.push_back({ node.Key, node.Value });

    for (const PropertyNode& child : node.Children)
        Append(child, depth + 1, everything, rows);
}

PropertyEditResult PropertySheet::Set(IPropertySource& source, std::size_t index, std::string_view value)
{
    if (index >= Rows.size())
        return PropertyEditResult::Gone;
    const Presented& row = Rows[index];
    if (value == row.Value)
        return PropertyEditResult::Unchanged;
    if (!source.BeginEdit(row.Key))
        return PropertyEditResult::Gone;
    return source.CommitEdit(row.Key, value);
}

PropertyEditResult PropertySheet::SetFromControl(IPropertySource& source, std::size_t index, const UiValue& reported)
{
    switch (reported.Kind())
    {
    case UiValueKind::Float: return Set(source, index, std::format("{}", reported.AsFloat()));
    case UiValueKind::Int:   return Set(source, index, std::format("{}", reported.AsInt()));
    case UiValueKind::Bool:  return Set(source, index, reported.AsBool() ? "true" : "false");
    default:                 return Set(source, index, reported.AsString());
    }
}

PropertyEditResult PropertySheet::Flip(IPropertySource& source, std::size_t index)
{
    if (index >= Rows.size())
        return PropertyEditResult::Gone;
    return Set(source, index, Rows[index].Value == "true" ? "false" : "true");
}
