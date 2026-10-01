#include "authoring/PreviewModelProperties.h"

#include "authoring/UiPreviewSession.h"
#include "properties/SettingProperties.h"

#include <array>
#include <charconv>
#include <cmath>
#include <format>

namespace
{
constexpr std::array<std::string_view, 5> kKinds{ "none", "bool", "int", "float", "string" };

std::size_t KindIndex(UiValueKind kind)
{
    switch (kind)
    {
    case UiValueKind::None:   return 0;
    case UiValueKind::Bool:   return 1;
    case UiValueKind::Int:
    case UiValueKind::Id:     return 2;
    case UiValueKind::Float:  return 3;
    case UiValueKind::String: return 4;
    }
    return 0;
}

UiValue DefaultOfKind(std::size_t index)
{
    switch (index)
    {
    case 1:  return UiValue(false);
    case 2:  return UiValue(std::int64_t{ 0 });
    case 3:  return UiValue(0.0);
    case 4:  return UiValue(std::string{});
    default: return UiValue{};
    }
}

std::vector<std::string_view> ControlNames()
{
    std::vector<std::string_view> names;
    for (std::uint8_t control = 0; control <= static_cast<std::uint8_t>(UiRowControl::LongText); ++control)
        names.push_back(UiRowControlName(static_cast<UiRowControl>(control)));
    return names;
}

// "rows.2.items.0.label" read as its parts.
std::vector<std::string_view> Parts(std::string_view key)
{
    std::vector<std::string_view> parts;
    for (std::size_t start = 0; start <= key.size();)
    {
        const std::size_t dot = std::min(key.find('.', start), key.size());
        parts.push_back(key.substr(start, dot - start));
        start = dot + 1;
    }
    return parts;
}

// The element `part` names in `items`, or null.
template <typename T>
T* At(std::vector<T>& items, std::string_view part)
{
    std::size_t index = 0;
    if (std::from_chars(part.data(), part.data() + part.size(), index).ec != std::errc{} || index >= items.size())
        return nullptr;
    return &items[index];
}

template <typename T>
bool EraseAt(std::vector<T>& items, std::string_view part)
{
    T* item = At(items, part);
    if (item == nullptr)
        return false;
    items.erase(items.begin() + (item - items.data()));
    return true;
}

PropertyNode WithVerbs(PropertyNode node, std::vector<PropertyVerb> verbs)
{
    node.Descriptor.Verbs = std::move(verbs);
    return node;
}

PropertyVerb Remove()
{
    return { "remove", "Remove" };
}

PropertyNode Sample(const std::string& key, const UiValue& value)
{
    switch (value.Kind())
    {
    case UiValueKind::Bool:   return ToggleProperty(key, "Sample", value.AsBool());
    case UiValueKind::Int:
    case UiValueKind::Id:     return NumberProperty(key, "Sample", static_cast<double>(value.AsInt()), 0.0, -1.0, 1.0);
    case UiValueKind::Float:  return NumberProperty(key, "Sample", value.AsFloat(), 0.0, -1.0, 0.0);
    case UiValueKind::String: return TextProperty(key, "Sample", std::string(value.AsString()));
    case UiValueKind::None:   break;
    }
    return NoteProperty(key, "Sample", "none");
}

std::optional<UiValue> ParseSample(UiValueKind kind, std::string_view text)
{
    switch (kind)
    {
    case UiValueKind::Bool:
        if (const std::optional<bool> on = ParseToggle(text))
            return UiValue(*on);
        return std::nullopt;
    case UiValueKind::Int:
    case UiValueKind::Id:
        if (const std::optional<double> number = ParseNumber(text))
            return UiValue(static_cast<std::int64_t>(std::llround(*number)));
        return std::nullopt;
    case UiValueKind::Float:
        if (const std::optional<double> number = ParseNumber(text))
            return UiValue(*number);
        return std::nullopt;
    case UiValueKind::String: return UiValue(std::string(text));
    case UiValueKind::None:   return std::nullopt;
    }
    return std::nullopt;
}

std::string JoinChoices(const std::vector<std::string>& choices)
{
    std::string out;
    for (const std::string& choice : choices)
        out += (out.empty() ? "" : ", ") + choice;
    return out;
}

std::vector<std::string> SplitChoices(std::string_view text)
{
    std::vector<std::string> out;
    for (std::size_t start = 0; start <= text.size();)
    {
        const std::size_t comma = std::min(text.find(',', start), text.size());
        std::string_view item = text.substr(start, comma - start);
        const std::size_t first = item.find_first_not_of(' ');
        if (first != std::string_view::npos)
            out.emplace_back(item.substr(first, item.find_last_not_of(' ') - first + 1));
        start = comma + 1;
    }
    return out;
}

UiRow SampleRow()
{
    return UiRow{ "Row", "value", "" };
}

std::vector<PropertyNode> RowNodes(const std::string& key, const UiRow& row)
{
    const std::vector<std::string_view> controls = ControlNames();
    std::vector<PropertyNode> nodes{
        TextProperty(key + ".label", "Label", row.Label),
        TextProperty(key + ".value", "Value", row.Value),
        TextProperty(key + ".detail", "Detail", row.Detail),
        ToggleProperty(key + ".editable", "Editable", row.Editable, "The document may offer a control for it"),
        ChoiceProperty(key + ".control", "Control", controls, static_cast<std::size_t>(row.Control)),
    };
    if (row.Control == UiRowControl::Range)
    {
        nodes.push_back(NumberProperty(key + ".number", "Number", row.Number, 0.0, -1.0, 0.0));
        nodes.push_back(NumberProperty(key + ".min", "Min", row.Min, 0.0, -1.0, 0.0));
        nodes.push_back(NumberProperty(key + ".max", "Max", row.Max, 0.0, -1.0, 0.0));
        nodes.push_back(NumberProperty(key + ".step", "Step", row.Step, 0.0, -1.0, 0.0));
    }
    if (row.Control == UiRowControl::Choice)
        nodes.push_back(TextProperty(key + ".choices", "Choices", JoinChoices(row.Choices),
                                     "Comma-separated labels; the value should be one of them"));
    return nodes;
}

bool SetPropertyField(UiModelProperty& property, std::string_view field, std::string_view value)
{
    if (field == "name")
    {
        property.Path = value;
        return true;
    }
    if (field == "writable")
    {
        const std::optional<bool> on = ParseToggle(value);
        if (on)
            property.Editable = *on;
        return on.has_value();
    }
    if (field == "kind")
    {
        const std::optional<std::size_t> kind = ParseChoice(value, kKinds);
        if (kind && *kind != KindIndex(property.Initial.Kind()))
            property.Initial = DefaultOfKind(*kind);
        return kind.has_value();
    }
    if (field == "sample")
    {
        const std::optional<UiValue> sample = ParseSample(property.Initial.Kind(), value);
        if (sample)
            property.Initial = *sample;
        return sample.has_value();
    }
    return false;
}

bool SetRowField(UiRow& row, std::string_view field, std::string_view value)
{
    if (field == "label") row.Label = value;
    else if (field == "value") row.Value = value;
    else if (field == "detail") row.Detail = value;
    else if (field == "choices") row.Choices = SplitChoices(value);
    else if (field == "editable")
    {
        const std::optional<bool> on = ParseToggle(value);
        if (!on)
            return false;
        row.Editable = *on;
    }
    else if (field == "control")
    {
        const std::optional<UiRowControl> control = ParseUiRowControl(value);
        if (!control)
            return false;
        row.Control = *control;
    }
    else
    {
        double* number = field == "number" ? &row.Number
                         : field == "min"  ? &row.Min
                         : field == "max"  ? &row.Max
                         : field == "step" ? &row.Step
                                           : nullptr;
        const std::optional<double> parsed = number != nullptr ? ParseNumber(value) : std::nullopt;
        if (!parsed)
            return false;
        *number = *parsed;
    }
    return true;
}
} // namespace

void PreviewModelProperties::MarkSaved()
{
    Saved = Session.Model();
}

bool PreviewModelProperties::Changed() const
{
    return !(Session.Model() == Saved);
}

std::vector<PropertyNode> PreviewModelProperties::Nodes() const
{
    const UiPreviewModel& model = Session.Model();
    std::vector<PropertyNode> nodes{
        TextProperty("model.name", "Data model", model.ModelName, "The name the document's data-model attribute binds"),
        ToggleProperty("model.modal", "Modal", model.Modal, "Opened modal: it takes input from everything beneath"),
    };

    PropertyNode properties = WithVerbs(GroupProperty("properties", "Properties"), { { "add", "Add Property" } });
    for (std::size_t i = 0; i < model.Properties.size(); ++i)
    {
        const UiModelProperty& property = model.Properties[i];
        const std::string key = std::format("properties.{}", i);
        properties.Children.push_back(WithVerbs(
            GroupProperty(key, property.Path,
                          { TextProperty(key + ".name", "Name", property.Path),
                            ChoiceProperty(key + ".kind", "Kind", kKinds, KindIndex(property.Initial.Kind())),
                            Sample(key + ".sample", property.Initial),
                            ToggleProperty(key + ".writable", "Writable", property.Editable,
                                           "A control may write it back") }),
            { Remove() }));
    }
    nodes.push_back(std::move(properties));

    PropertyNode lists = WithVerbs(GroupProperty("lists", "Lists"), { { "add", "Add List" } });
    for (std::size_t i = 0; i < model.Arrays.size(); ++i)
    {
        const UiPreviewModel::ArraySample& list = model.Arrays[i];
        const std::string key = std::format("lists.{}", i);
        PropertyNode group = WithVerbs(GroupProperty(key, list.Name, { TextProperty(key + ".name", "Name", list.Name) }),
                                       { { "add_item", "Add Item" }, Remove() });
        for (std::size_t j = 0; j < list.Items.size(); ++j)
            group.Children.push_back(WithVerbs(
                TextProperty(std::format("{}.items.{}", key, j), std::format("Item {}", j + 1), list.Items[j]),
                { Remove() }));
        lists.Children.push_back(std::move(group));
    }
    nodes.push_back(std::move(lists));

    PropertyNode rowLists = WithVerbs(GroupProperty("rows", "Row lists"), { { "add", "Add Row List" } });
    for (std::size_t i = 0; i < model.Rows.size(); ++i)
    {
        const UiPreviewModel::RowsSample& rows = model.Rows[i];
        const std::string key = std::format("rows.{}", i);
        PropertyNode group = WithVerbs(GroupProperty(key, rows.Name, { TextProperty(key + ".name", "Name", rows.Name) }),
                                       { { "add_row", "Add Row" }, Remove() });
        for (std::size_t j = 0; j < rows.Items.size(); ++j)
        {
            const std::string rowKey = std::format("{}.items.{}", key, j);
            const UiRow& row = rows.Items[j];
            group.Children.push_back(WithVerbs(
                GroupProperty(rowKey, row.Label.empty() ? std::string("(row)") : row.Label, RowNodes(rowKey, row)),
                { Remove() }));
        }
        rowLists.Children.push_back(std::move(group));
    }
    nodes.push_back(std::move(rowLists));

    PropertyNode actions = WithVerbs(GroupProperty("actions", "Actions"), { { "add", "Add Action" } });
    for (std::size_t i = 0; i < model.Actions.size(); ++i)
        actions.Children.push_back(WithVerbs(
            TextProperty(std::format("actions.{}", i), std::format("Action {}", i + 1), model.Actions[i]), { Remove() }));
    nodes.push_back(std::move(actions));
    return nodes;
}

bool PreviewModelProperties::BeginEdit(std::string_view)
{
    Before = Session.Model();
    return true;
}

PropertyEditResult PreviewModelProperties::PreviewEdit(std::string_view key, std::string_view value)
{
    return Show(Apply(key, value));
}

PropertyEditResult PreviewModelProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const PropertyEditResult result = Show(Apply(key, value));
    Before.reset();
    return result;
}

void PreviewModelProperties::CancelEdit(std::string_view)
{
    if (!Before)
        return;
    Session.Model() = std::move(*Before);
    Before.reset();
    (void)Session.Reopen();
}

PreviewModelProperties::Applied PreviewModelProperties::Apply(std::string_view key, std::string_view value)
{
    UiPreviewModel next = Session.Model();
    const std::vector<std::string_view> parts = Parts(key);
    Reach touched = Reach::Declaration;
    bool understood = false;

    if (parts.size() == 2 && parts[0] == "model")
    {
        if (parts[1] == "name")
        {
            next.ModelName = value;
            understood = true;
        }
        else if (const std::optional<bool> on = ParseToggle(value); parts[1] == "modal" && on)
        {
            next.Modal = *on;
            understood = true;
        }
    }
    else if (UiModelProperty* property = parts.size() == 3 && parts[0] == "properties" ? At(next.Properties, parts[1])
                                                                                        : nullptr)
    {
        understood = SetPropertyField(*property, parts[2], value);
    }
    else if (UiPreviewModel::ArraySample* list = parts.size() >= 3 && parts[0] == "lists" ? At(next.Arrays, parts[1])
                                                                                          : nullptr)
    {
        if (parts.size() == 3 && parts[2] == "name")
        {
            list->Name = value;
            understood = true;
        }
        else if (std::string* item = parts.size() == 4 && parts[2] == "items" ? At(list->Items, parts[3]) : nullptr)
        {
            *item = value;
            understood = true;
            touched = Reach::Samples;
        }
    }
    else if (UiPreviewModel::RowsSample* rows = parts.size() >= 3 && parts[0] == "rows" ? At(next.Rows, parts[1])
                                                                                        : nullptr)
    {
        if (parts.size() == 3 && parts[2] == "name")
        {
            rows->Name = value;
            understood = true;
        }
        else if (UiRow* row = parts.size() == 5 && parts[2] == "items" ? At(rows->Items, parts[3]) : nullptr)
        {
            understood = SetRowField(*row, parts[4], value);
            touched = Reach::Samples;
        }
    }
    else if (std::string* action = parts.size() == 2 && parts[0] == "actions" ? At(next.Actions, parts[1]) : nullptr)
    {
        *action = value;
        understood = true;
    }

    if (!understood)
        return { PropertyEditResult::Refused, touched };
    if (next == Session.Model())
        return { PropertyEditResult::Unchanged, touched };
    Session.Model() = std::move(next);
    return { PropertyEditResult::Applied, touched };
}

PropertyEditResult PreviewModelProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    UiPreviewModel& model = Session.Model();
    const std::vector<std::string_view> parts = Parts(key);
    Applied applied{ PropertyEditResult::Applied, Reach::Declaration };
    if (verb == "add" && parts.size() == 1)
    {
        if (key == "properties")
            model.Properties.push_back(UiModelProperty{ .Path = "new_property", .Initial = UiValue(std::string{}) });
        else if (key == "lists")
            model.Arrays.push_back({ "new_list", { "first", "second" } });
        else if (key == "rows")
            model.Rows.push_back({ "new_rows", { SampleRow() } });
        else if (key == "actions")
            model.Actions.emplace_back("new_action");
        else
            applied.Result = PropertyEditResult::Gone;
    }
    else if (verb == "remove" && parts.size() == 2)
    {
        const bool removed = parts[0] == "properties" ? EraseAt(model.Properties, parts[1])
                             : parts[0] == "lists"    ? EraseAt(model.Arrays, parts[1])
                             : parts[0] == "rows"     ? EraseAt(model.Rows, parts[1])
                             : parts[0] == "actions"  ? EraseAt(model.Actions, parts[1])
                                                      : false;
        applied.Result = removed ? PropertyEditResult::Applied : PropertyEditResult::Gone;
    }
    else if (verb == "remove" && parts.size() == 4 && parts[2] == "items")
    {
        applied.Touched = Reach::Samples;
        bool removed = false;
        if (UiPreviewModel::ArraySample* list = parts[0] == "lists" ? At(model.Arrays, parts[1]) : nullptr)
            removed = EraseAt(list->Items, parts[3]);
        else if (UiPreviewModel::RowsSample* rows = parts[0] == "rows" ? At(model.Rows, parts[1]) : nullptr)
            removed = EraseAt(rows->Items, parts[3]);
        applied.Result = removed ? PropertyEditResult::Applied : PropertyEditResult::Gone;
    }
    else if (UiPreviewModel::ArraySample* list =
                 verb == "add_item" && parts.size() == 2 && parts[0] == "lists" ? At(model.Arrays, parts[1]) : nullptr)
    {
        list->Items.emplace_back("item");
        applied.Touched = Reach::Samples;
    }
    else if (UiPreviewModel::RowsSample* rows =
                 verb == "add_row" && parts.size() == 2 && parts[0] == "rows" ? At(model.Rows, parts[1]) : nullptr)
    {
        rows->Items.push_back(SampleRow());
        applied.Touched = Reach::Samples;
    }
    else
        applied.Result = PropertyEditResult::Gone;
    return Show(applied);
}

PropertyEditResult PreviewModelProperties::Show(Applied applied)
{
    if (applied.Result != PropertyEditResult::Applied)
        return applied.Result;
    if (applied.Touched == Reach::Declaration)
        (void)Session.Reopen();
    else
        Session.Model().Publish(Session.Service(), Session.OpenScreen());
    return applied.Result;
}
