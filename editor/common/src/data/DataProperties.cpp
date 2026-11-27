#include "data/DataProperties.h"

#include "data/DataDocument.h"
#include "data/DataDocumentSet.h"
#include "data/DataPatch.h"
#include "data/DataPath.h"
#include "data/DataSubtypeAuthoring.h"
#include "identity/RandomKey.h"
#include "properties/SettingProperties.h"

#include <core/assets/AssetRefCodec.h>
#include <core/composition/Composition.h>
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

std::string Label(const DataFieldSchema& field)
{
    return field.Units.empty() ? DataFieldDisplayName(field) : std::format("{} ({})", DataFieldDisplayName(field), field.Units);
}

const DataFieldSchema& Unwrapped(const DataFieldSchema& field)
{
    return *UnwrapDataField(&field);
}

// The field `path` names, read from the schema alone (not unwrapped).
const DataFieldSchema* FieldAt(const DataFieldSchema& root, std::string_view path)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!steps.has_value())
        return nullptr;
    const DataFieldSchema* field = &root;
    for (const DataPathStep& step : *steps)
    {
        const DataFieldSchema& reading = Unwrapped(*field);
        field = step.Kind == DataPathStepKind::Member ? FindChild(reading, step.Key)
              : reading.Kind == DataFieldKind::Array  ? DataFieldElement(reading)
                                                      : nullptr;
        if (field == nullptr)
            return nullptr;
    }
    return field;
}

std::string ChoiceLabel(const DataEnumChoice& choice)
{
    return choice.DisplayName.empty() ? choice.Value : choice.DisplayName;
}

std::string Text(const JsonValue& value, const DataFieldSchema& field)
{
    switch (field.Kind)
    {
    case DataFieldKind::AssetRef:
    case DataFieldKind::DataAssetRef:
    {
        const std::optional<AssetRef> ref = AssetRefFromJson(value);
        return ref.has_value() ? ref->Path : std::string{};
    }
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

std::string ElementName(const DataFieldSchema& array)
{
    const DataFieldSchema* element = DataFieldElement(array);
    return element == nullptr || element->DisplayName.empty() ? std::string("Element") : element->DisplayName;
}

std::string ElementLabel(const DataFieldSchema& array, const JsonValue& item, std::string fallback)
{
    if (const JsonValue* title = !array.Editor.TitleKey.empty() && item.IsObject() ? item.Find(array.Editor.TitleKey)
                                                                                   : nullptr;
        title != nullptr && title->IsString() && !title->AsString().empty())
        return title->AsString();
    return fallback;
}

std::string NumberedLabel(const DataFieldSchema& array, std::size_t index)
{
    return std::format("{} {}", ElementName(array), index + 1);
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

struct Inheritance
{
    const CompositionReport& Report;
    const JsonValue& Inherited;
    const DataFieldSchema& Root;
    std::vector<std::string> Includes;
};

std::optional<Inheritance> InheritanceOf(const DataDocument& document, const DataSchema& schema)
{
    const CompositionReport* report = document.CompositionResult();
    if (report == nullptr || !document.ComposedData().IsObject())
        return std::nullopt;
    Inheritance inheritance{ *report, document.InheritedData(), schema.Root, {} };
    if (const JsonValue* includes = document.Root().Find("includes"); includes != nullptr && includes->IsArray())
        for (const JsonValue& entry : includes->AsArray())
            if (const std::optional<AssetRef> ref = AssetRefFromJson(entry); ref && !ref->Path.empty())
                inheritance.Includes.push_back(ref->Path);
    return inheritance;
}

const JsonValue* ComposedOf(const DataDocument& document)
{
    return document.CompositionResult() != nullptr && document.ComposedData().IsObject() ? &document.ComposedData()
                                                                                         : nullptr;
}

std::string ReportPath(std::string_view path)
{
    return "$" + std::string(path.substr(kDataPathRoot.size()));
}

std::string Provenance(const CompositionOrigin& origin)
{
    switch (origin.Layer)
    {
    case CompositionLayer::Local:   return "set here";
    case CompositionLayer::Base:    return std::format("inherited from {}", origin.Asset);
    case CompositionLayer::Include: return std::format("from {}", origin.Asset);
    }
    return {};
}

void Freeze(PropertyNode& node)
{
    node.Descriptor.ReadOnly = true;
    node.Descriptor.Verbs.clear();
    for (PropertyNode& child : node.Children)
        Freeze(child);
}

class Layout
{
public:
    Layout(std::span<const DataValidationError> errors, const Inheritance* inheritance)
        : Errors(errors)
        , Composing(inheritance)
    {
    }

    void Members(std::vector<PropertyNode>& out, const JsonValue& value, const DataFieldSchema& record,
                 const std::string& path) const
    {
        for (const DataFieldSchema& member : record.Children)
        {
            const std::string memberPath = path + "." + member.Key;
            const JsonValue* held = value.IsObject() ? value.Find(member.Key) : nullptr;
            if (held == nullptr && IsRemoved(memberPath))
            {
                if (const JsonValue* inherited = FindDataValue(Composing->Inherited, Composing->Root, memberPath))
                    RemovedRows(out, *inherited, member, memberPath, Label(member));
                continue;
            }
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
            const DataFieldSchema* child = DataFieldElement(field);
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
                    absent.push_back(DataFieldDisplayName(member));
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
        Annotate(node, path);
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

    void Annotate(PropertyNode& node, const std::string& path) const
    {
        if (Composing == nullptr || path == kDataPathRoot)
            return;
        const std::string reported = ReportPath(path);
        const CompositionOrigin* origin = FindCompositionOrigin(Composing->Report.Provenance, reported);
        if (origin == nullptr)
            return;
        const std::string from = Provenance(*origin);
        node.Descriptor.Tooltip =
            node.Descriptor.Tooltip.empty() ? from : std::format("{} — {}", from, node.Descriptor.Tooltip);
        if (origin->Layer == CompositionLayer::Local
            && FindDataValue(Composing->Inherited, Composing->Root, path) != nullptr)
            node.Descriptor.Verbs.push_back({ .Id = "revert", .Label = "Revert to inherited" });
        const bool conflicted = std::ranges::any_of(Composing->Report.Diagnostics, [&](const CompositionDiagnostic& found) {
            return found.Code == "include_conflict" && found.Path == reported;
        });
        if (conflicted && !Composing->Includes.empty())
            node.Descriptor.Verbs.push_back({ .Id = "take", .Label = "Take value from", .Choices = Composing->Includes });
    }

    [[nodiscard]] bool IsRemoved(const std::string& path) const
    {
        return Composing != nullptr && std::ranges::find(Composing->Report.Removed, ReportPath(path))
                                           != Composing->Report.Removed.end();
    }

    static void RemovedRows(std::vector<PropertyNode>& out, const JsonValue& value, const DataFieldSchema& field,
                            const std::string& path, std::string label)
    {
        const std::size_t first = out.size();
        Layout(std::span<const DataValidationError>{}, nullptr).Field(out, value, field, path, std::move(label));
        for (std::size_t row = first; row < out.size(); ++row)
        {
            Freeze(out[row]);
            out[row].Descriptor.Tooltip = "removed here";
        }
        if (first < out.size())
            out[first].Descriptor.Verbs.push_back({ .Id = "restore", .Label = "Restore" });
    }

    void Elements(PropertyNode& array, const JsonValue& value, const DataFieldSchema& field, const std::string& path) const
    {
        const DataFieldSchema* element = DataFieldElement(field);
        if (element == nullptr)
            return;
        array.Descriptor.Verbs.push_back({ .Id = "add", .Label = "Add " + ElementName(field) });
        if (const DataFieldSchema* key = Composing != nullptr ? ArrayMergeKey(field) : nullptr)
        {
            KeyedElements(array, value, field, *element, *key, path);
            return;
        }
        const std::size_t count = value.IsArray() ? value.AsArray().size() : 0;
        for (std::size_t index = 0; index < count; ++index)
        {
            const JsonValue& item = value.AsArray()[index];
            const std::size_t first = array.Children.size();
            Field(array.Children, item, *element, std::format("{}[{}]", path, index),
                  ElementLabel(field, item, NumberedLabel(field, index)));
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

    void KeyedElements(PropertyNode& array, const JsonValue& value, const DataFieldSchema& field,
                       const DataFieldSchema& element, const DataFieldSchema& key, const std::string& path) const
    {
        const JsonValue* inherited = FindDataValue(Composing->Inherited, Composing->Root, path);
        const bool appendOnly = field.MergePolicy == DataMergePolicy::AppendOnly;
        const JsonValue::Array none;
        for (const JsonValue& item : value.IsArray() ? value.AsArray() : none)
        {
            const JsonValue* stated = item.IsObject() ? item.Find(key.Key) : nullptr;
            if (stated == nullptr || !stated->IsString())
                continue;
            const std::string name = stated->AsString();
            const std::string elementPath = path + "{" + name + "}";
            const std::size_t first = array.Children.size();
            Field(array.Children, item, element, elementPath, ElementLabel(field, item, name));
            if (first >= array.Children.size())
                continue;
            PropertyNode& card = array.Children[first];
            const bool isInherited = inherited != nullptr && FindKeyedElement(*inherited, field, name) != nullptr;
            if (appendOnly && isInherited)
                Freeze(card);
            else
                card.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove" });
            if (isInherited)
                for (PropertyNode& row : card.Children)
                    if (row.Key == elementPath + "." + key.Key)
                        row.Descriptor.ReadOnly = true;
        }
        if (inherited == nullptr)
            return;
        const std::string under = ReportPath(path) + "{";
        for (const std::string& removed : Composing->Report.Removed)
        {
            if (!removed.starts_with(under) || !removed.ends_with('}'))
                continue;
            const std::string name = removed.substr(under.size(), removed.size() - under.size() - 1);
            if (name.find_first_of(".[{}") != std::string::npos)
                continue;
            if (const JsonValue* item = FindKeyedElement(*inherited, field, name))
                RemovedRows(array.Children, *item, element, path + "{" + name + "}", ElementLabel(field, *item, name));
        }
    }

    std::span<const DataValidationError> Errors;
    const Inheritance* Composing = nullptr;
};

PropertyEditResult RemoveComposed(JsonValue& data, const DataFieldSchema& root, const JsonValue* composed,
                                  const JsonValue& inherited, std::string_view path)
{
    if (FindDataValue(inherited, root, path) == nullptr)
        return EraseFromPatch(data, root, path) ? PropertyEditResult::Applied : PropertyEditResult::Gone;
    const std::optional<std::string_view> parent = DataPathParent(path);
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    if (!parent || !steps || steps->empty() || steps->back().Kind == DataPathStepKind::Index)
        return PropertyEditResult::Refused;
    const DataPatchLocation holder = LocateInPatch(data, composed, root, *parent, true);
    if (holder.Value == nullptr)
        return PropertyEditResult::Gone;
    const std::string& name = steps->back().Key;
    if (holder.Value->IsArray())
    {
        const JsonValue* element = FindKeyedElement(*holder.Value, Unwrapped(*holder.Field), name);
        if (element == nullptr)
            return PropertyEditResult::Gone;
        JsonValue::Array& elements = holder.Value->AsArray();
        elements.erase(elements.begin() + (element - elements.data()));
        return PropertyEditResult::Applied;
    }
    if (!holder.Value->IsObject())
        return PropertyEditResult::Gone;
    if (JsonValue* held = holder.Value->Find(name))
        *held = JsonValue();
    else
        holder.Value->AsObject().emplace_back(name, JsonValue());
    return PropertyEditResult::Applied;
}

std::optional<std::string_view> RenamedElement(std::string_view path, const DataFieldSchema& root)
{
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(path);
    const std::optional<std::string_view> element = DataPathParent(path);
    if (!steps || steps->size() < 2 || !element || (*steps)[steps->size() - 2].Kind != DataPathStepKind::Keyed)
        return std::nullopt;
    const std::optional<std::string_view> list = DataPathParent(*element);
    const DataFieldSchema* field = list ? FieldAt(root, *list) : nullptr;
    const DataFieldSchema* key = field != nullptr ? ArrayMergeKey(Unwrapped(*field)) : nullptr;
    if (key == nullptr || key->Key != steps->back().Key)
        return std::nullopt;
    return element;
}
} // namespace

std::string DataFieldDisplayName(const DataFieldSchema& field)
{
    return field.DisplayName.empty() ? field.Key : field.DisplayName;
}

std::vector<PropertyNode> DataProperties::Nodes() const
{
    const DataDocument* document = Shown ? Shown() : nullptr;
    const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
    const JsonValue* local = document != nullptr ? document->Data() : nullptr;
    if (schema == nullptr || local == nullptr)
        return {};
    const std::optional<Inheritance> inheritance = InheritanceOf(*document, *schema);
    const JsonValue& data = inheritance ? document->ComposedData() : *local;
    std::vector<PropertyNode> nodes;
    const Layout layout(document->ValidationErrors(), inheritance ? &*inheritance : nullptr);
    if (const std::string scope = Scope ? Scope() : std::string{}; !scope.empty())
    {
        const DataFieldSchema* field = FieldAt(schema->Root, scope);
        const JsonValue* value = FindDataValue(data, schema->Root, scope);
        const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(scope);
        if (field == nullptr || value == nullptr || !steps || steps->empty())
            return {};
        const std::optional<std::string_view> parent = DataPathParent(scope);
        const DataFieldSchema* list = parent ? FieldAt(schema->Root, *parent) : nullptr;
        const DataPathStep& last = steps->back();
        std::string label = Label(Unwrapped(*field));
        if (list != nullptr && last.Kind == DataPathStepKind::Index)
            label = ElementLabel(Unwrapped(*list), *value, NumberedLabel(Unwrapped(*list), last.Index));
        else if (list != nullptr && last.Kind == DataPathStepKind::Keyed)
            label = ElementLabel(Unwrapped(*list), *value, last.Key);
        layout.Field(nodes, *value, *field, scope, std::move(label));
    }
    else
    {
        // The root as one group named for the asset's type, so its absent optional
        // members have somewhere to be added from.
        layout.Field(nodes, data, schema->Root, std::string(kDataPathRoot),
                     schema->DisplayName.empty() ? schema->TypeName : schema->DisplayName);
    }
    if (const IDataSubtypeAuthoring* authoring = AuthoringOf(*document))
        authoring->Refine(nodes, *document, *schema);
    return nodes;
}

IDataSubtypeAuthoring* DataProperties::AuthoringOf(const DataDocument& document) const
{
    return Subtypes != nullptr ? Subtypes->Find(document.Subtype()) : nullptr;
}

PropertyEditResult DataProperties::Rewrite(const Rewriter& rewrite, DataEdit edit)
{
    DataDocument* document = Shown ? Shown() : nullptr;
    const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
    if (schema == nullptr)
        return PropertyEditResult::Gone;
    JsonValue root = document->CopyRoot();
    JsonValue* data = root.Find("data");
    if (data == nullptr)
        return PropertyEditResult::Gone;
    const PropertyEditResult result = rewrite(*data, *document, schema->Root);
    if (result == PropertyEditResult::Applied)
        Documents.ApplyEdit(*document, std::move(root), edit);
    return result;
}

PropertyEditResult DataProperties::Change(std::string_view key,
                                          const std::function<bool(JsonValue&, const DataFieldSchema&)>& change,
                                          DataEdit edit)
{
    return Rewrite([&](JsonValue& data, const DataDocument& document, const DataFieldSchema& root) {
        const DataPatchLocation at = LocateInPatch(data, ComposedOf(document), root, key, true);
        if (at.Value == nullptr)
            return PropertyEditResult::Gone;
        return change(*at.Value, *at.Field) ? PropertyEditResult::Applied : PropertyEditResult::Refused;
    }, edit);
}

PropertyEditResult DataProperties::Replace(JsonValue data, DataEdit edit)
{
    return Change(kDataPathRoot, [&](JsonValue& held, const DataFieldSchema&) {
        held = std::move(data);
        return true;
    }, edit);
}

bool DataProperties::BeginEdit(std::string_view key)
{
    DataDocument* document = Shown ? Shown() : nullptr;
    const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
    if (schema == nullptr || document->Data() == nullptr)
        return false;
    const std::string_view path = key.ends_with(kSwitch) ? key.substr(0, key.size() - 1) : key;
    // A key the schema does not lay out may be one the subtype writes itself.
    const DataFieldSchema* field = FieldAt(schema->Root, path);
    if (field != nullptr ? field->ReadOnly : AuthoringOf(*document) == nullptr)
        return false;
    document->BeginEdit();
    return true;
}

PropertyEditResult DataProperties::PreviewEdit(std::string_view key, std::string_view value)
{
    if (const DataDocument* document = Shown ? Shown() : nullptr)
        if (const IDataSubtypeAuthoring* authoring = AuthoringOf(*document))
            if (const DataSchema* schema = Documents.Store().SchemaOf(*document); schema && document->Data())
            {
                JsonValue data = *document->Data();
                const DataSubtypeEdit written = authoring->Write(key, value, data, *schema);
                if (written == DataSubtypeEdit::Applied)
                    return Replace(std::move(data), DataEdit::Preview);
                if (written != DataSubtypeEdit::NotMine)
                    return PropertyEditResultOf(written);
            }
    if (key.ends_with(kSwitch))
        return Change(key.substr(0, key.size() - 1), [value](JsonValue& held, const DataFieldSchema& field) {
            const std::optional<bool> on = ParseToggle(value);
            const DataFieldSchema* child = DataFieldElement(field);
            if (!on.has_value() || child == nullptr)
                return false;
            held = *on ? CreateDefaultDataValue(*child) : JsonValue();
            return true;
        }, DataEdit::Preview);
    if (const DataDocument* document = Shown ? Shown() : nullptr; document != nullptr && ComposedOf(*document))
        if (const DataSchema* schema = Documents.Store().SchemaOf(*document))
            if (const std::optional<std::string_view> element = RenamedElement(key, schema->Root))
                return Rewrite([&](JsonValue& data, const DataDocument& shown, const DataFieldSchema& root) {
                    return RenameKeyedElement(data, shown.InheritedData(), root, *element, value)
                        ? PropertyEditResult::Applied
                        : PropertyEditResult::Refused;
                }, DataEdit::Preview);
    return Change(key, [this, value](JsonValue& held, const DataFieldSchema& field) {
        std::optional<JsonValue> parsed = Parse(value, Unwrapped(field));
        if (!parsed.has_value())
            return false;
        const DataFieldKind kind = Unwrapped(field).Kind;
        // A reference chosen by its path is written with its asset's id.
        if ((kind == DataFieldKind::AssetRef || kind == DataFieldKind::DataAssetRef) && !value.empty())
            parsed = AssetRefToJson(Documents.Store().Current(AssetRef{ .Path = std::string(value) }));
        held = std::move(*parsed);
        return true;
    }, DataEdit::Preview);
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
    Documents.Store().CommitEdit(*document);
    return previewed;
}

void DataProperties::CancelEdit(std::string_view)
{
    if (DataDocument* document = Shown ? Shown() : nullptr)
        Documents.Store().CancelEdit(*document);
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
    const DataFieldSchema* field = FieldAt(schema->Root, key);
    if (field == nullptr || Unwrapped(*field).Kind != DataFieldKind::DataAssetRef)
        return {};
    return Documents.Store().DataAssetPaths(Unwrapped(*field).Reference.DataSubtype);
}

PropertyEditResult DataProperties::Act(std::string_view key, std::string_view verb, std::string_view choice)
{
    if (const DataDocument* document = Shown ? Shown() : nullptr)
        if (IDataSubtypeAuthoring* authoring = AuthoringOf(*document))
        {
            if (const DataSubtypeEdit taken = authoring->Act(key, verb, *document); taken != DataSubtypeEdit::NotMine)
                return PropertyEditResultOf(taken);
            JsonValue changed = document->Data() != nullptr ? *document->Data() : JsonValue();
            const DataSubtypeEdit performed = authoring->Perform(key, verb, choice, changed);
            if (performed == DataSubtypeEdit::Applied)
                return Replace(std::move(changed), DataEdit::Step);
            if (performed != DataSubtypeEdit::NotMine)
                return PropertyEditResultOf(performed);
        }
    const DataDocument* document = Shown ? Shown() : nullptr;
    const DataSchema* schema = document != nullptr ? Documents.Store().SchemaOf(*document) : nullptr;
    if (schema == nullptr || document->Data() == nullptr)
        return PropertyEditResult::Gone;
    if (verb == "open")
    {
        const JsonValue& data = ComposedOf(*document) != nullptr ? document->ComposedData() : *document->Data();
        const JsonValue* held = FindDataValue(data, schema->Root, key);
        const std::optional<AssetRef> ref = held != nullptr ? AssetRefFromJson(*held) : std::nullopt;
        if (!ref || ref->Path.empty())
            return PropertyEditResult::Gone;
        std::string error;
        return Documents.OpenOrFocus(ref->Path, error) != nullptr ? PropertyEditResult::Applied
                                                                  : PropertyEditResult::Refused;
    }
    if (verb == "revert" || verb == "restore")
        return Rewrite([&](JsonValue& data, const DataDocument&, const DataFieldSchema& root) {
            return EraseFromPatch(data, root, key) ? PropertyEditResult::Applied : PropertyEditResult::Gone;
        }, DataEdit::Step);
    if (verb == "take")
    {
        const std::optional<JsonValue> fragment = Documents.Store().ComposedData(choice);
        const JsonValue* taken = fragment ? FindDataValue(*fragment, schema->Root, key) : nullptr;
        if (taken == nullptr)
            return PropertyEditResult::Refused;
        return Change(key, [value = *taken](JsonValue& held, const DataFieldSchema&) {
            held = value;
            return true;
        }, DataEdit::Step);
    }
    if (verb == "remove" && ComposedOf(*document) != nullptr)
        return Rewrite([&](JsonValue& data, const DataDocument& shown, const DataFieldSchema& root) {
            return RemoveComposed(data, root, ComposedOf(shown), shown.InheritedData(), key);
        }, DataEdit::Step);
    if (verb == "add")
        return Change(key, [choice](JsonValue& held, const DataFieldSchema& described) {
            const DataFieldSchema& field = Unwrapped(described);
            if (field.Kind == DataFieldKind::Array)
            {
                const DataFieldSchema* element = DataFieldElement(field);
                if (element == nullptr)
                    return false;
                JsonValue fresh = CreateDefaultDataValue(*element);
                if (const DataFieldSchema* keyField = ArrayMergeKey(field); keyField != nullptr && held.IsObject())
                {
                    JsonValue* stated = fresh.IsObject() ? fresh.Find(keyField->Key) : nullptr;
                    if (stated == nullptr)
                        return false;
                    if (!stated->IsString() || stated->AsString().empty())
                        *stated = JsonValue(MintStableKey());
                    const std::string name = stated->AsString();
                    held.AsObject().emplace_back(name, std::move(fresh));
                    return true;
                }
                if (!held.IsArray())
                    held = JsonValue(JsonValue::Array{});
                held.AsArray().push_back(std::move(fresh));
                return true;
            }
            const auto member = std::ranges::find_if(field.Children, [choice](const DataFieldSchema& child) {
                return DataFieldDisplayName(child) == choice;
            });
            if (field.Kind != DataFieldKind::Record || member == field.Children.end())
                return false;
            if (!held.IsObject())
                held = JsonValue(JsonValue::Object{});
            if (held.Find(member->Key) == nullptr)
                held.AsObject().emplace_back(member->Key, CreateDefaultDataValue(*member));
            return true;
        }, DataEdit::Step);

    const std::optional<std::string_view> parent = DataPathParent(key);
    const std::optional<std::vector<DataPathStep>> steps = ParseDataPath(key);
    if (!parent.has_value() || !steps.has_value())
        return PropertyEditResult::Gone;
    const DataPathStep step = steps->back();
    return Change(*parent, [&](JsonValue& held, const DataFieldSchema& described) {
        if (verb == "remove" && step.Kind == DataPathStepKind::Member && held.IsObject())
        {
            std::erase_if(held.AsObject(), [&](const auto& member) { return member.first == step.Key; });
            return true;
        }
        if (step.Kind != DataPathStepKind::Index || !held.IsArray() || step.Index >= held.AsArray().size())
            return false;
        JsonValue::Array& array = held.AsArray();
        const std::size_t index = step.Index;
        const auto at = array.begin() + static_cast<std::ptrdiff_t>(index);
        if (verb == "duplicate")
        {
            JsonValue copy = *at;
            if (const DataFieldSchema* element = DataFieldElement(Unwrapped(described)))
                RemintStableKeys(copy, *element);
            array.insert(at + 1, std::move(copy));
        }
        else if (verb == "delete")
            array.erase(at);
        else if (verb == "up" && index > 0)
            std::swap(array[index], array[index - 1]);
        else if (verb == "down" && index + 1 < array.size())
            std::swap(array[index], array[index + 1]);
        else
            return false;
        return true;
    }, DataEdit::Step);
}
