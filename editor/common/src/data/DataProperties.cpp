#include "data/DataProperties.h"

#include "data/DataDocument.h"
#include "data/DataDocumentSet.h"
#include "data/DataPath.h"
#include "data/DataSubtypeAuthoring.h"
#include "properties/SettingProperties.h"

#include <core/metadata/DataSchema.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>

namespace
{
// Suffixes naming what sits beside a field rather than the field: an optional
// member's switch, and a validation error's note.
constexpr char kSwitch = '?';
constexpr char kProblem = '!';

std::string DisplayName(const DataFieldSchema& field)
{
    return field.DisplayName.empty() ? field.Key : field.DisplayName;
}

std::string Label(const DataFieldSchema& field)
{
    return field.Units.empty() ? DisplayName(field) : std::format("{} ({})", DisplayName(field), field.Units);
}

const DataFieldSchema* Element(const DataFieldSchema& field)
{
    return field.Children.size() == 1 ? &field.Children.front() : nullptr;
}

const DataFieldSchema* Member(const DataFieldSchema& record, std::string_view key)
{
    const auto found = std::ranges::find(record.Children, key, &DataFieldSchema::Key);
    return found != record.Children.end() ? &*found : nullptr;
}

// The schema a value is read by: an optional member's value is its child's.
const DataFieldSchema& Unwrapped(const DataFieldSchema& field)
{
    const DataFieldSchema* child = field.Kind == DataFieldKind::Optional ? Element(field) : nullptr;
    return child != nullptr ? *child : field;
}

struct Located
{
    JsonValue* Value = nullptr;
    const DataFieldSchema* Field = nullptr;
};

// The value at `path` under `data` and the field that describes it (not
// unwrapped). With `create`, a missing member is made from its default on the
// way, as writing a required member the file does not yet hold must.
Located Locate(JsonValue& data, const DataFieldSchema& root, std::string_view path, bool create)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value())
        return {};
    JsonValue* value = &data;
    const DataFieldSchema* field = &root;
    for (const DataPathStep& step : *steps)
    {
        const DataFieldSchema& reading = Unwrapped(*field);
        if (step.Index.has_value())
        {
            const DataFieldSchema* element = Element(reading);
            if (reading.Kind != DataFieldKind::Array || element == nullptr || !value->IsArray()
                || *step.Index >= value->AsArray().size())
                return {};
            value = &value->AsArray()[*step.Index];
            field = element;
            continue;
        }
        const DataFieldSchema* member = Member(reading, step.Key);
        if (member == nullptr)
            return {};
        if (!value->IsObject())
        {
            if (!create)
                return {};
            *value = JsonValue(JsonValue::Object{});
        }
        JsonValue* next = value->Find(step.Key);
        if (next == nullptr)
        {
            if (!create)
                return {};
            value->AsObject().emplace_back(step.Key, CreateDefaultDataValue(*member));
            next = &value->AsObject().back().second;
        }
        value = next;
        field = member;
    }
    return { value, field };
}

// The path up to the last step, and that step.
std::optional<std::pair<std::string, DataPathStep>> Parent(std::string_view path)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value() || steps->empty())
        return std::nullopt;
    const std::size_t cut = steps->back().Index.has_value() ? path.rfind('[') : path.rfind('.');
    return std::pair{ std::string(path.substr(0, cut)), steps->back() };
}

std::string ChoiceLabel(const DataEnumChoice& choice)
{
    return choice.DisplayName.empty() ? choice.Value : choice.DisplayName;
}

std::string Text(const JsonValue& value, const DataFieldSchema& field)
{
    switch (field.Kind)
    {
    case DataFieldKind::Bool:
        return value.IsBool() && value.AsBool() ? "true" : "false";
    case DataFieldKind::Int:
        return value.IsNumber() ? std::format("{}", static_cast<long long>(std::llround(value.AsNumber()))) : "0";
    case DataFieldKind::Float:
        return value.IsNumber() ? std::format("{}", value.AsNumber()) : "0";
    case DataFieldKind::Enum:
    {
        const std::string current = value.IsString() ? value.AsString() : std::string{};
        const auto found = std::ranges::find(field.EnumChoices, current, &DataEnumChoice::Value);
        return found != field.EnumChoices.end() ? ChoiceLabel(*found) : current;
    }
    case DataFieldKind::Vector:
    {
        std::string text;
        if (value.IsArray())
            for (const JsonValue& component : value.AsArray())
                text += std::format("{}{}", text.empty() ? "" : ", ", component.IsNumber() ? component.AsNumber() : 0.0);
        return text;
    }
    default:
        return value.IsString() ? value.AsString() : std::string{};
    }
}

std::optional<JsonValue> Parse(std::string_view text, const DataFieldSchema& field)
{
    switch (field.Kind)
    {
    case DataFieldKind::Bool:
        if (const std::optional<bool> on = ParseToggle(text))
            return JsonValue(*on);
        return std::nullopt;
    case DataFieldKind::Int:
    case DataFieldKind::Float:
    {
        const bool bounded = field.Numeric.Minimum.has_value() && field.Numeric.Maximum.has_value();
        const std::optional<double> number =
            ParseNumber(text, bounded ? *field.Numeric.Minimum : 0.0, bounded ? *field.Numeric.Maximum : -1.0);
        if (!number.has_value())
            return std::nullopt;
        return JsonValue(field.Kind == DataFieldKind::Int ? std::round(*number) : *number);
    }
    case DataFieldKind::Enum:
        for (const DataEnumChoice& choice : field.EnumChoices)
            if (ChoiceLabel(choice) == text || choice.Value == text)
                return JsonValue(choice.Value);
        return std::nullopt;
    case DataFieldKind::Vector:
    {
        const std::optional<std::vector<double>> components = ParseVector(text, std::min<std::uint32_t>(field.VectorLength, 4));
        if (!components.has_value())
            return std::nullopt;
        JsonValue::Array array;
        for (const double component : *components)
            array.emplace_back(component);
        return JsonValue(std::move(array));
    }
    case DataFieldKind::Record:
    case DataFieldKind::Array:
    case DataFieldKind::Optional:
        return std::nullopt;
    default:
        return JsonValue(std::string(text));
    }
}

PropertyKind KindOf(const DataFieldSchema& field)
{
    switch (field.Kind)
    {
    case DataFieldKind::Bool:         return PropertyKind::Toggle;
    case DataFieldKind::Int:
    case DataFieldKind::Float:        return PropertyKind::Number;
    case DataFieldKind::Enum:         return PropertyKind::Choice;
    case DataFieldKind::Vector:       return PropertyKind::Vector;
    case DataFieldKind::DataAssetRef: return PropertyKind::Asset;
    case DataFieldKind::Record:
    case DataFieldKind::Array:        return PropertyKind::Group;
    default:                          return PropertyKind::Text;
    }
}

class Layout
{
public:
    explicit Layout(std::span<const DataValidationError> errors) : Errors(errors) {}

    void Members(std::vector<PropertyNode>& out, const JsonValue& value, const DataFieldSchema& record,
                 const std::string& path) const
    {
        for (const DataFieldSchema& member : record.Children)
        {
            const std::string memberPath = path + "." + member.Key;
            const JsonValue* held = value.IsObject() ? value.Find(member.Key) : nullptr;
            if (held == nullptr && !member.Required)
                continue;
            const JsonValue shown = held != nullptr ? *held : CreateDefaultDataValue(member);
            const std::size_t first = out.size();
            Field(out, shown, member, memberPath, Label(member));
            if (!member.Required && first < out.size())
                out[first].Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove" });
        }
    }

    void Field(std::vector<PropertyNode>& out, const JsonValue& value, const DataFieldSchema& field,
               const std::string& path, std::string label) const
    {
        if (field.Kind == DataFieldKind::Optional)
        {
            const DataFieldSchema* child = Element(field);
            PropertyNode on = ToggleProperty(path + kSwitch, std::move(label), !value.IsNull(), field.Summary);
            Describe(on, field);
            out.push_back(std::move(on));
            if (child != nullptr && !value.IsNull())
                Field(out, value, *child, path, Label(*child));
            return;
        }
        PropertyNode node;
        node.Key = path;
        node.Descriptor.Kind = KindOf(field);
        node.Descriptor.Label = std::move(label);
        Describe(node, field);
        if (field.Kind == DataFieldKind::Record)
        {
            Members(node.Children, value, field, path);
            std::vector<std::string> absent;
            for (const DataFieldSchema& member : field.Children)
                if (!member.Required && (!value.IsObject() || value.Find(member.Key) == nullptr))
                    absent.push_back(DisplayName(member));
            if (!absent.empty())
                node.Descriptor.Verbs.push_back({ .Id = "add", .Label = "Add", .Choices = std::move(absent) });
        }
        else if (field.Kind == DataFieldKind::Array)
            Elements(node, value, field, path);
        else
        {
            node.Value = Text(value, field);
            if (field.Kind == DataFieldKind::Enum)
            {
                for (const DataEnumChoice& choice : field.EnumChoices)
                    node.Descriptor.Choices.push_back(ChoiceLabel(choice));
                // A value no choice names is offered as it is, so showing it
                // changes nothing.
                if (std::ranges::find(node.Descriptor.Choices, node.Value) == node.Descriptor.Choices.end())
                    node.Descriptor.Choices.push_back(node.Value);
            }
            if (field.Kind == DataFieldKind::DataAssetRef && !node.Value.empty())
                node.Descriptor.Verbs.push_back({ .Id = "open", .Label = "Open" });
        }
        out.push_back(std::move(node));
        for (const DataValidationError& error : Errors)
            if (error.Path == path)
                out.push_back(NoteProperty(path + kProblem, "Problem", error.Message));
    }

private:
    static void Describe(PropertyNode& node, const DataFieldSchema& field)
    {
        node.Descriptor.Tooltip = field.Summary;
        node.Descriptor.ReadOnly = field.ReadOnly;
        node.Descriptor.Advanced = field.Advanced;
        node.Descriptor.Multiline = field.Editor.Multiline;
        if (field.Numeric.Minimum.has_value() && field.Numeric.Maximum.has_value())
        {
            node.Descriptor.Min = *field.Numeric.Minimum;
            node.Descriptor.Max = *field.Numeric.Maximum;
        }
        node.Descriptor.Step = field.Numeric.Step.value_or(field.Kind == DataFieldKind::Int ? 1.0 : 0.0);
    }

    void Elements(PropertyNode& array, const JsonValue& value, const DataFieldSchema& field, const std::string& path) const
    {
        const DataFieldSchema* element = Element(field);
        if (element == nullptr)
            return;
        const std::string name = element->DisplayName.empty() ? std::string("Element") : element->DisplayName;
        array.Descriptor.Verbs.push_back({ .Id = "add", .Label = "Add " + name });
        const std::size_t count = value.IsArray() ? value.AsArray().size() : 0;
        for (std::size_t index = 0; index < count; ++index)
        {
            const JsonValue& item = value.AsArray()[index];
            // Counted from one and named after the element, or by what the
            // author called it when the schema says which member does.
            std::string label = std::format("{} {}", name, index + 1);
            if (const JsonValue* title = !field.Editor.TitleKey.empty() && item.IsObject() ? item.Find(field.Editor.TitleKey)
                                                                                           : nullptr;
                title != nullptr && title->IsString() && !title->AsString().empty())
                label = title->AsString();
            const std::size_t first = array.Children.size();
            Field(array.Children, item, *element, std::format("{}[{}]", path, index), std::move(label));
            if (first >= array.Children.size())
                continue;
            std::vector<PropertyVerb>& verbs = array.Children[first].Descriptor.Verbs;
            verbs.push_back({ .Id = "duplicate", .Label = "Duplicate" });
            verbs.push_back({ .Id = "delete", .Label = "Delete" });
            if (index > 0)
                verbs.push_back({ .Id = "up", .Label = "Up" });
            if (index + 1 < count)
                verbs.push_back({ .Id = "down", .Label = "Down" });
        }
    }

    std::span<const DataValidationError> Errors;
};
} // namespace

std::vector<PropertyNode> DataProperties::Nodes() const
{
    const DataDocument* document = Shown ? Shown() : nullptr;
    const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
    const JsonValue* data = document != nullptr ? document->Data() : nullptr;
    if (schema == nullptr || data == nullptr)
        return {};
    // The root as one group named for the asset's type, so its absent optional
    // members have somewhere to be added from.
    std::vector<PropertyNode> nodes;
    Layout(document->ValidationErrors())
        .Field(nodes, *data, schema->Root, std::string(kDataPathRoot),
               schema->DisplayName.empty() ? schema->TypeName : schema->DisplayName);
    if (const IDataSubtypeAuthoring* authoring = AuthoringOf(*document))
        authoring->Refine(nodes, *document, *schema);
    return nodes;
}

IDataSubtypeAuthoring* DataProperties::AuthoringOf(const DataDocument& document) const
{
    return Subtypes != nullptr ? Subtypes->Find(document.Subtype()) : nullptr;
}

PropertyEditResult DataProperties::Change(std::string_view key,
                                          const std::function<bool(JsonValue&, const DataFieldSchema&)>& change,
                                          bool step)
{
    DataDocument* document = Shown ? Shown() : nullptr;
    const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
    if (schema == nullptr)
        return PropertyEditResult::Gone;
    JsonValue root = document->CopyRoot();
    JsonValue* data = root.Find("data");
    if (data == nullptr)
        return PropertyEditResult::Gone;
    const Located at = Locate(*data, schema->Root, key, true);
    if (at.Value == nullptr)
        return PropertyEditResult::Gone;
    if (!change(*at.Value, *at.Field))
        return PropertyEditResult::Refused;
    if (step || !document->IsEditing())
        document->BeginEdit();
    document->PreviewRoot(std::move(root));
    Documents.EditPreviewed(*document);
    if (step)
    {
        document->CommitEdit();
        Documents.EditCommitted(*document);
    }
    return PropertyEditResult::Applied;
}

bool DataProperties::BeginEdit(std::string_view key)
{
    DataDocument* document = Shown ? Shown() : nullptr;
    const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
    const JsonValue* data = document != nullptr ? document->Data() : nullptr;
    if (schema == nullptr || data == nullptr)
        return false;
    const std::string_view path = key.ends_with(kSwitch) ? key.substr(0, key.size() - 1) : key;
    JsonValue scratch = *data;
    const IDataSubtypeAuthoring* authoring = AuthoringOf(*document);
    const bool subtypes = authoring != nullptr && authoring->Write(key, {}, scratch, *schema).has_value();
    const Located at = subtypes ? Located{} : Locate(scratch, schema->Root, path, true);
    if (!subtypes && (at.Field == nullptr || at.Field->ReadOnly))
        return false;
    if (!document->IsEditing())
        document->BeginEdit();
    return true;
}

PropertyEditResult DataProperties::PreviewEdit(std::string_view key, std::string_view value)
{
    if (const DataDocument* document = Shown ? Shown() : nullptr)
        if (const IDataSubtypeAuthoring* authoring = AuthoringOf(*document))
        {
            const DataSchema* schema = Documents.Store().SchemaOf(*document);
            JsonValue probe = document->Data() != nullptr ? *document->Data() : JsonValue();
            if (schema != nullptr && authoring->Write(key, value, probe, *schema).has_value())
                return Change(kDataPathRoot, [&](JsonValue& data, const DataFieldSchema&) {
                    return authoring->Write(key, value, data, *schema).value_or(false);
                }, false);
        }
    if (key.ends_with(kSwitch))
        return Change(key.substr(0, key.size() - 1), [value](JsonValue& held, const DataFieldSchema& field) {
            const std::optional<bool> on = ParseToggle(value);
            const DataFieldSchema* child = Element(field);
            if (!on.has_value() || child == nullptr)
                return false;
            held = *on ? CreateDefaultDataValue(*child) : JsonValue();
            return true;
        }, false);
    return Change(key, [value](JsonValue& held, const DataFieldSchema& field) {
        std::optional<JsonValue> parsed = Parse(value, Unwrapped(field));
        if (!parsed.has_value())
            return false;
        held = std::move(*parsed);
        return true;
    }, false);
}

PropertyEditResult DataProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const PropertyEditResult previewed = PreviewEdit(key, value);
    DataDocument* document = Shown ? Shown() : nullptr;
    if (document == nullptr || !document->IsEditing())
        return previewed;
    if (previewed == PropertyEditResult::Refused || previewed == PropertyEditResult::Gone)
    {
        CancelEdit(key);
        return previewed;
    }
    document->CommitEdit();
    Documents.EditCommitted(*document);
    return previewed;
}

void DataProperties::CancelEdit(std::string_view)
{
    DataDocument* document = Shown ? Shown() : nullptr;
    if (document == nullptr || !document->IsEditing())
        return;
    document->CancelEdit();
    Documents.EditPreviewed(*document);
}

std::vector<std::string> DataProperties::Candidates(std::string_view key) const
{
    const DataDocument* document = Shown ? Shown() : nullptr;
    const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
    const JsonValue* data = document != nullptr ? document->Data() : nullptr;
    if (schema == nullptr || data == nullptr)
        return {};
    if (const IDataSubtypeAuthoring* authoring = AuthoringOf(*document))
        if (std::optional<std::vector<std::string>> own = authoring->Candidates(key, *data))
            return *own;
    JsonValue scratch = *data;
    const Located at = Locate(scratch, schema->Root, key, true);
    if (at.Field == nullptr || Unwrapped(*at.Field).Kind != DataFieldKind::DataAssetRef)
        return {};
    return Documents.DataAssetPaths(Unwrapped(*at.Field).Reference.DataSubtype);
}

PropertyEditResult DataProperties::Act(std::string_view key, std::string_view verb, std::string_view choice)
{
    if (const DataDocument* document = Shown ? Shown() : nullptr)
        if (IDataSubtypeAuthoring* authoring = AuthoringOf(*document))
            if (const std::optional<PropertyEditResult> taken = authoring->Act(key, verb, *document))
                return *taken;
    if (verb == "open")
    {
        const DataDocument* document = Shown ? Shown() : nullptr;
        const JsonValue* data = document != nullptr ? document->Data() : nullptr;
        const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
        if (data == nullptr || schema == nullptr)
            return PropertyEditResult::Gone;
        JsonValue scratch = *data;
        const Located at = Locate(scratch, schema->Root, key, false);
        if (at.Value == nullptr || !at.Value->IsString() || at.Value->AsString().empty())
            return PropertyEditResult::Gone;
        Documents.OpenDataAsset(at.Value->AsString());
        return PropertyEditResult::Applied;
    }
    if (verb == "add")
        return Change(key, [choice](JsonValue& held, const DataFieldSchema& described) {
            const DataFieldSchema& field = Unwrapped(described);
            if (field.Kind == DataFieldKind::Array)
            {
                const DataFieldSchema* element = Element(field);
                if (element == nullptr)
                    return false;
                if (!held.IsArray())
                    held = JsonValue(JsonValue::Array{});
                held.AsArray().push_back(CreateDefaultDataValue(*element));
                return true;
            }
            const auto member = std::ranges::find_if(field.Children, [choice](const DataFieldSchema& child) {
                return DisplayName(child) == choice;
            });
            if (field.Kind != DataFieldKind::Record || member == field.Children.end())
                return false;
            if (!held.IsObject())
                held = JsonValue(JsonValue::Object{});
            if (held.Find(member->Key) == nullptr)
                held.AsObject().emplace_back(member->Key, CreateDefaultDataValue(*member));
            return true;
        }, true);

    const std::optional<std::pair<std::string, DataPathStep>> parent = Parent(key);
    if (!parent.has_value())
        return PropertyEditResult::Gone;
    const DataPathStep step = parent->second;
    return Change(parent->first, [&](JsonValue& held, const DataFieldSchema&) {
        if (verb == "remove" && !step.Index.has_value() && held.IsObject())
        {
            std::erase_if(held.AsObject(), [&](const auto& member) { return member.first == step.Key; });
            return true;
        }
        if (!step.Index.has_value() || !held.IsArray() || *step.Index >= held.AsArray().size())
            return false;
        JsonValue::Array& array = held.AsArray();
        const std::size_t index = *step.Index;
        const auto at = array.begin() + static_cast<std::ptrdiff_t>(index);
        if (verb == "duplicate")
            array.insert(at + 1, *at);
        else if (verb == "delete")
            array.erase(at);
        else if (verb == "up" && index > 0)
            std::swap(array[index], array[index - 1]);
        else if (verb == "down" && index + 1 < array.size())
            std::swap(array[index], array[index + 1]);
        else
            return false;
        return true;
    }, true);
}
