#include "presenters/PropertySheet.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace
{
// Beside a heading there is room for this many actions; the rest are on the
// context wheel and in the palette.
constexpr std::size_t kActionsBeside = 3;

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

// "r, g, b" in 0..1, as a colour a document can paint a swatch with; HDR
// channels show at full.
std::string SwatchOf(std::string_view text)
{
    int channels[3] = { 0, 0, 0 };
    for (int& channel : channels)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == ','))
            text.remove_prefix(1);
        double value = 0.0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
        channel = static_cast<int>(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5);
    }
    return std::format("rgba({}, {}, {}, 255)", channels[0], channels[1], channels[2]);
}
} // namespace

std::vector<UiRow> PropertySheet::Present(const IPropertySource& source, bool everything)
{
    Rows.clear();
    std::vector<UiRow> rows;
    for (const PropertyNode& node : source.Nodes())
        Append(node, 0, 0, everything, rows);
    Settle();
    return rows;
}

void PropertySheet::Settle()
{
    std::vector<std::string> layout;
    layout.reserve(Rows.size());
    for (const Presented& row : Rows)
        layout.push_back(row.Layout);
    Settling = layout != LaidOut;
    LaidOut = std::move(layout);
}

std::vector<UiRow> PropertySheet::Present(std::span<const PropertySection> sections, bool everything)
{
    Rows.clear();
    std::vector<UiRow> rows;
    for (std::size_t section = 0; section < sections.size(); ++section)
    {
        if (sections[section].Source == nullptr)
            continue;
        const std::vector<PropertyNode> nodes = sections[section].Source->Nodes();
        if (nodes.empty())
            continue;
        if (!sections[section].Heading.empty())
        {
            UiRow heading{ sections[section].Heading, "", "" };
            heading.Control = UiRowControl::Section;
            rows.push_back(std::move(heading));
            Rows.push_back({ .Key = {}, .Value = {}, .Section = section });
        }
        for (const PropertyNode& node : nodes)
            Append(node, 0, section, everything, rows);
    }
    Settle();
    return rows;
}

void PropertySheet::OfferActions(const ActionCatalog& catalog, const ActionContextStack& actions)
{
    Catalog = &catalog;
    Actions = &actions;
}

bool PropertySheet::FoldedNow(const PropertyNode& node) const
{
    const auto folded = Folds.find(node.Key);
    return folded != Folds.end() ? folded->second : node.Descriptor.Folded;
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
    row.Control = descriptor.Kind == PropertyKind::Text && descriptor.Multiline ? UiRowControl::LongText
                                                                               : ControlFor(descriptor);
    row.Editable = !descriptor.ReadOnly && descriptor.Kind != PropertyKind::Group
                && descriptor.Kind != PropertyKind::Unsupported;
    row.Choices = descriptor.Choices;
    if (row.Control == UiRowControl::Color)
        row.Parts = { SwatchOf(node.Value) };
    if (row.Control == UiRowControl::Range)
    {
        row.Number = NumberOf(node.Value);
        row.Min = descriptor.Min;
        row.Max = descriptor.Max;
        row.Step = descriptor.Step > 0.0 ? descriptor.Step : (descriptor.Max - descriptor.Min) / 100.0;
    }
    // A group that holds something says whether it shows it, the way an
    // outline's branch does.
    const bool foldable = descriptor.Kind == PropertyKind::Group && !node.Children.empty();
    const bool folded = foldable && FoldedNow(node);
    row.Expanded = foldable && !folded;
    if (foldable)
        row.Value = folded ? "closed" : "open";

    std::string layout = node.Key;
    for (const std::string& choice : descriptor.Choices)
        layout += "|" + choice;
    std::vector<std::string> actions;
    if (Catalog != nullptr && Actions != nullptr)
        for (const std::string& id : descriptor.Actions)
        {
            const ActionDefinition* definition = Catalog->Find(id);
            const ActionContextStack::State state = Actions->Query(id);
            if (definition == nullptr || !state.Claimed || !state.Enabled || actions.size() == kActionsBeside)
                continue;
            row.Verbs.push_back({ .Key = id, .Label = definition->Label });
            actions.push_back(id);
        }
    for (const PropertyVerb& verb : descriptor.Verbs)
    {
        row.Verbs.push_back({ .Key = verb.Id, .Label = verb.Label, .Removes = verb.Removes });
        layout += "#" + verb.Id;
    }
    rows.push_back(std::move(row));
    Rows.push_back({ .Key = node.Key,
                     .Value = node.Value,
                     .Section = section,
                     .Label = descriptor.Label,
                     .Verbs = descriptor.Verbs,
                     .Actions = std::move(actions),
                     .Foldable = foldable,
                     .Folded = folded,
                     .Snap = rows.back().Control == UiRowControl::Range ? rows.back().Step : 0.0,
                     .Layout = std::move(layout) });
    if (folded)
        return;
    for (const PropertyNode& child : node.Children)
        Append(child, depth + 1, section, everything, rows);
}

void PropertySheet::Fold(std::size_t index)
{
    if (index >= Rows.size() || !Rows[index].Foldable)
        return;
    Folds[Rows[index].Key] = !Rows[index].Folded;
}

IPropertySource* PropertySheet::SourceOf(std::span<const PropertySection> sections, std::size_t index) const
{
    if (index >= Rows.size() || Rows[index].Section >= sections.size())
        return nullptr;
    return sections[Rows[index].Section].Source;
}

PropertyEditResult PropertySheet::Set(IPropertySource& source, std::size_t index, std::string_view value)
{
    if (index >= Rows.size() || Rows[index].Key.empty())
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

namespace
{
bool WithinSnap(const UiValue& reported, std::string_view presented, double snap)
{
    if (snap <= 0.0)
        return false;
    double value = 0.0;
    switch (reported.Kind())
    {
    case UiValueKind::Float:  value = reported.AsFloat(); break;
    case UiValueKind::Int:    value = static_cast<double>(reported.AsInt()); break;
    case UiValueKind::String:
    {
        const std::string_view text = reported.AsString();
        if (std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc{})
            return false;
        break;
    }
    default: return false;
    }
    return std::abs(value - NumberOf(presented)) <= snap * 0.5;
}
} // namespace

PropertyEditResult PropertySheet::SetFromControl(IPropertySource& source, std::size_t index, const UiValue& reported)
{
    if (Settling)
        return PropertyEditResult::Unchanged;
    if (index < Rows.size() && WithinSnap(reported, Rows[index].Value, Rows[index].Snap))
        return PropertyEditResult::Unchanged;
    return Set(source, index, ReportedText(reported));
}

PropertyEditResult PropertySheet::SetFromControl(std::span<const PropertySection> sections, std::size_t index,
                                                 const UiValue& reported)
{
    if (Settling)
        return PropertyEditResult::Unchanged;
    if (index < Rows.size() && WithinSnap(reported, Rows[index].Value, Rows[index].Snap))
        return PropertyEditResult::Unchanged;
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

PropertyEditResult PropertySheet::Act(IPropertySource& source, std::size_t index, std::string_view verb,
                                      std::string_view choice)
{
    if (index >= Rows.size())
        return PropertyEditResult::Gone;
    const Presented& row = Rows[index];
    if (std::ranges::find(row.Actions, verb) != row.Actions.end())
        return Actions != nullptr && Actions->Invoke(verb) ? PropertyEditResult::Applied : PropertyEditResult::Gone;
    if (std::ranges::find(row.Verbs, verb, &PropertyVerb::Id) == row.Verbs.end())
        return PropertyEditResult::Gone;
    return source.Act(row.Key, verb, choice);
}

PropertyEditResult PropertySheet::Act(std::span<const PropertySection> sections, std::size_t index,
                                      std::string_view verb, std::string_view choice)
{
    IPropertySource* source = SourceOf(sections, index);
    return source != nullptr ? Act(*source, index, verb, choice) : PropertyEditResult::Gone;
}

std::vector<std::string> PropertySheet::ChoicesOf(std::size_t index, std::string_view verb) const
{
    if (index >= Rows.size())
        return {};
    const auto found = std::ranges::find(Rows[index].Verbs, verb, &PropertyVerb::Id);
    return found != Rows[index].Verbs.end() ? found->Choices : std::vector<std::string>{};
}

std::vector<std::string> PropertySheet::NotesOf(std::size_t index, std::string_view verb) const
{
    if (index >= Rows.size())
        return {};
    const auto found = std::ranges::find(Rows[index].Verbs, verb, &PropertyVerb::Id);
    return found != Rows[index].Verbs.end() ? found->ChoiceNotes : std::vector<std::string>{};
}

bool PropertySheet::Removes(std::size_t index, std::string_view verb) const
{
    if (index >= Rows.size())
        return false;
    const auto found = std::ranges::find(Rows[index].Verbs, verb, &PropertyVerb::Id);
    return found != Rows[index].Verbs.end() && found->Removes;
}

std::string PropertySheet::LabelOf(std::size_t index) const
{
    return index < Rows.size() ? Rows[index].Label : std::string{};
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
