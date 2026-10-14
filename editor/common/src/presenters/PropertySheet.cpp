#include "presenters/PropertySheet.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "presenters/PlotPeaks.h"
#include "presenters/RowControls.h"
#include "properties/ColorValue.h"
#include "properties/PropertyEditSession.h"
#include "properties/SettingProperties.h"
#include "text/PathLeaf.h"
#include "ui/DocumentLengths.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace
{
// Inside a group there is room for this many actions; the rest are on the
// context wheel and in the palette.
constexpr std::size_t kActionsInside = 3;
constexpr std::size_t kPlotBars = 40;

std::string_view ControlFor(const PropertyDescriptor& descriptor)
{
    switch (descriptor.Kind)
    {
    case PropertyKind::Group:  return RowControls::Section;
    case PropertyKind::Choice: return RowControls::Choice;
    case PropertyKind::Toggle: return RowControls::Toggle;
    case PropertyKind::Asset:  return RowControls::Asset;
    case PropertyKind::Color:  return RowControls::Color;
    case PropertyKind::Number: return RowControls::Number;
    case PropertyKind::Vector: return RowControls::Vector;
    case PropertyKind::Plot:   return RowControls::Plot;
    case PropertyKind::Text:
    case PropertyKind::Unsupported:
    default:                   return RowControls::Text;
    }
}

std::string AxisName(std::size_t index, std::size_t count)
{
    static constexpr std::string_view kAxes[] = { "X", "Y", "Z", "W" };
    return count <= std::size(kAxes) ? std::string(kAxes[index]) : std::to_string(index);
}

} // namespace

std::vector<UiRow> PropertySheet::Present(const IPropertySource& source, bool everything)
{
    Rows.clear();
    Focused.reset();
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
    Focused.reset();
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
            heading.Control = RowControls::Section;
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

bool PropertySheet::Holds(std::span<const PropertyNode> nodes, std::string_view key)
{
    return std::ranges::any_of(nodes, [&](const PropertyNode& node) {
        return node.Key == key || Holds(node.Children, key);
    });
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
    row.Control = descriptor.Kind == PropertyKind::Text && descriptor.Multiline ? RowControls::LongText
                                                                               : ControlFor(descriptor);
    row.Editable = !descriptor.ReadOnly && descriptor.Kind != PropertyKind::Group
                && descriptor.Kind != PropertyKind::Unsupported;
    row.Choices = descriptor.Choices;
    row.Mixed = node.Mixed;
    if (row.Control == RowControls::Color)
        row.Parts = { ColorSwatch(ParseColorValue(node.Value).value_or(ColorValue{}).Color) };
    if (row.Control == RowControls::Asset && !node.Value.empty())
        row.Parts = { std::string(PathLeaf(node.Value)) };
    if (row.Control == RowControls::Number)
    {
        row.Number = ParseNumber(node.Value).value_or(0.0);
        row.Min = descriptor.Min;
        row.Max = descriptor.Max;
        row.Step = descriptor.Step;
    }
    if (row.Control == RowControls::Plot)
    {
        std::vector<TimelineSample> samples;
        for (const std::string& part : VectorParts(node.Value))
            samples.push_back({ .Time = static_cast<double>(samples.size()), .Value = ParseNumber(part).value_or(0.0) });
        row.Parts.clear();
        if (!samples.empty())
            for (const double peak : PlotPeaks(samples, 0.0, samples.back().Time, kPlotBars))
                row.Parts.push_back(PercentLength(peak));
        row.Value.clear();
        row.Editable = false;
    }
    // A vector's numbers each with its name; one standing for several shows nothing.
    std::vector<std::string> elementKeys;
    if (row.Control == RowControls::Vector)
    {
        row.Parts.clear();
        row.Choices.clear();
        if (!node.Children.empty())
            for (const PropertyNode& element : node.Children)
            {
                row.Parts.push_back(element.Mixed ? std::string{} : element.Value);
                row.Choices.push_back(element.Descriptor.Label);
                elementKeys.push_back(element.Key);
            }
        else
        {
            row.Parts = VectorParts(node.Value);
            for (std::size_t i = 0; i < row.Parts.size(); ++i)
                row.Choices.push_back(AxisName(i, row.Parts.size()));
        }
    }
    // A group that holds something says whether it shows it, the way an
    // outline's branch does.
    const bool foldable = descriptor.Kind == PropertyKind::Group && !node.Children.empty();
    if (foldable && !FocusKey.empty() && Holds(node.Children, FocusKey))
        Folds[node.Key] = false;
    const bool folded = foldable && FoldedNow(node);
    if (!FocusKey.empty() && node.Key == FocusKey)
    {
        Focused = rows.size();
        row.Selected = true;
    }
    row.Expanded = foldable && !folded;
    if (foldable)
        row.Value = folded ? "closed" : "open";

    std::string layout = node.Key;
    for (const std::string& choice : descriptor.Choices)
        layout += "|" + choice;
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
                     .Foldable = foldable,
                     .Folded = folded,
                     .ElementKeys = std::move(elementKeys),
                     .Bounds = descriptor,
                     .ElementBounds = descriptor.Kind == PropertyKind::Vector && !node.Children.empty()
                                          ? node.Children.front().Descriptor
                                          : descriptor,
                     .Layout = std::move(layout) });
    if (folded)
        return;
    AppendActions(node, depth + 1, section, rows);
    // A vector's numbers are its own row, not rows under it.
    if (descriptor.Kind == PropertyKind::Vector)
        return;
    for (const PropertyNode& child : node.Children)
        Append(child, depth + 1, section, everything, rows);
}

void PropertySheet::AppendActions(const PropertyNode& node, int depth, std::size_t section, std::vector<UiRow>& rows)
{
    if (Catalog == nullptr || Actions == nullptr)
        return;
    UiRow row{ "", "", "" };
    row.Key = node.Key + "!actions";
    row.Depth = depth;
    row.Control = RowControls::Action;
    row.Editable = true;
    std::vector<std::string> actions;
    std::string layout = row.Key;
    for (const std::string& id : node.Descriptor.Actions)
    {
        const ActionDefinition* definition = Catalog->Find(id);
        const ActionContextStack::State state = Actions->Query(id);
        if (definition == nullptr || !state.Claimed || !state.Enabled || actions.size() == kActionsInside)
            continue;
        row.Verbs.push_back({ .Key = id, .Label = definition->Label });
        actions.push_back(id);
        layout += "#" + id;
    }
    if (actions.empty())
        return;
    rows.push_back(std::move(row));
    Rows.push_back({ .Key = {}, .Value = {}, .Section = section, .Actions = std::move(actions),
                     .Layout = std::move(layout) });
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

PropertyEditResult PropertySheet::SetElement(std::span<const PropertySection> sections, std::size_t index,
                                             std::size_t element, std::string_view value)
{
    IPropertySource* source = SourceOf(sections, index);
    if (source == nullptr)
        return PropertyEditResult::Gone;
    const Presented& row = Rows[index];
    if (element < row.ElementKeys.size())
        return PropertyEditSession{}.Set(*source, row.ElementKeys[element], value);
    // A vector without a key per number is set whole, the others as they were.
    std::vector<std::string> parts = VectorParts(row.Value);
    if (element >= parts.size())
        return PropertyEditResult::Gone;
    parts[element] = std::string(value);
    return Set(*source, index, JoinVector(parts));
}

PropertyEditResult PropertySheet::ReleaseElement(std::span<const PropertySection> sections, std::size_t index,
                                                 std::size_t element, std::string_view value)
{
    Held = {};
    return SetElement(sections, index, element, value);
}

std::string PropertySheet::EditKeyOf(std::size_t index, std::optional<std::size_t> element) const
{
    if (index >= Rows.size())
        return {};
    if (element.has_value())
        return *element < Rows[index].ElementKeys.size() ? Rows[index].ElementKeys[*element] : std::string{};
    return Rows[index].Key;
}

PropertyDescriptor PropertySheet::BoundsOf(std::size_t index, std::optional<std::size_t> element) const
{
    if (index >= Rows.size())
        return {};
    return element.has_value() ? Rows[index].ElementBounds : Rows[index].Bounds;
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
    if (Settling)
        return PropertyEditResult::Unchanged;
    return Set(source, index, ReportedText(reported));
}

PropertyEditResult PropertySheet::SetFromControl(std::span<const PropertySection> sections, std::size_t index,
                                                 const UiValue& reported)
{
    if (Settling)
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

const PropertyVerb* PropertySheet::VerbOf(std::size_t index, std::string_view verb) const
{
    if (index >= Rows.size())
        return nullptr;
    const auto found = std::ranges::find(Rows[index].Verbs, verb, &PropertyVerb::Id);
    return found != Rows[index].Verbs.end() ? &*found : nullptr;
}

bool PropertySheet::Removes(std::size_t index, std::string_view verb) const
{
    if (index >= Rows.size())
        return false;
    const auto found = std::ranges::find(Rows[index].Verbs, verb, &PropertyVerb::Id);
    return found != Rows[index].Verbs.end() && found->Removes;
}

std::optional<std::size_t> PropertySheet::IndexOf(std::string_view key) const
{
    const auto found = std::ranges::find(Rows, key, &Presented::Key);
    if (found == Rows.end())
        return std::nullopt;
    return static_cast<std::size_t>(found - Rows.begin());
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
    // The field a typed number becomes takes focus too: it stays a number.
    if (Held.Kind == HoldKind::Number && Held.Row == index)
        return;
    Held = index < Rows.size() ? HeldRow{ HoldKind::Field, index } : HeldRow{};
}

void PropertySheet::Type(std::size_t index, std::int64_t element)
{
    Held = index < Rows.size() ? HeldRow{ HoldKind::Number, index, element } : HeldRow{};
}

std::optional<PropertySheet::TypedNumber> PropertySheet::Typing() const
{
    if (Held.Kind != HoldKind::Number)
        return std::nullopt;
    return TypedNumber{ Held.Row, Held.Element };
}

PropertyEditResult PropertySheet::Release(IPropertySource& source, std::size_t index, std::string_view value)
{
    Held = {};
    return Set(source, index, value);
}

PropertyEditResult PropertySheet::Release(std::span<const PropertySection> sections, std::size_t index,
                                          std::string_view value)
{
    Held = {};
    return Set(sections, index, value);
}
