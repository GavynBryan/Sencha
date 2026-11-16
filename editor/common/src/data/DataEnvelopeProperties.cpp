#include "data/DataEnvelopeProperties.h"

#include "data/DataDocument.h"
#include "properties/SettingProperties.h"

#include <assets/data/DataDefinitionStaging.h>
#include <core/assets/AssetRefCodec.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>

namespace
{
constexpr std::string_view kExtends = "$.extends";
constexpr std::string_view kIncludes = "$.includes";
constexpr std::string_view kAbstract = "$.abstract";
constexpr char kProblem = '!';

std::string PathOf(const JsonValue* reference)
{
    const std::optional<AssetRef> ref = reference != nullptr ? AssetRefFromJson(*reference) : std::nullopt;
    return ref.has_value() ? ref->Path : std::string{};
}

std::optional<std::size_t> IncludeIndex(std::string_view key)
{
    if (!key.starts_with(kIncludes) || key.size() < kIncludes.size() + 3 || key[kIncludes.size()] != '['
        || key.back() != ']')
        return std::nullopt;
    std::size_t index = 0;
    const char* first = key.data() + kIncludes.size() + 1;
    const char* last = key.data() + key.size() - 1;
    const auto [end, error] = std::from_chars(first, last, index);
    return error == std::errc{} && end == last ? std::optional<std::size_t>(index) : std::nullopt;
}

JsonValue* MemberOf(JsonValue::Object& envelope, std::string_view key)
{
    const auto found = std::ranges::find(envelope, key, [](const auto& member) { return std::string_view(member.first); });
    return found != envelope.end() ? &found->second : nullptr;
}

void SetMember(JsonValue::Object& envelope, std::string_view key, JsonValue value)
{
    if (JsonValue* held = MemberOf(envelope, key))
    {
        *held = std::move(value);
        return;
    }
    const auto data = std::ranges::find(envelope, std::string_view("data"),
                                        [](const auto& member) { return std::string_view(member.first); });
    envelope.insert(data, { std::string(key), std::move(value) });
}

void EraseMember(JsonValue::Object& envelope, std::string_view key)
{
    std::erase_if(envelope, [&](const auto& member) { return member.first == key; });
}

void AppendProblems(std::vector<PropertyNode>& out, const DataDocument& document, std::string_view key)
{
    for (const DataValidationError& error : document.ValidationErrors())
        if (error.Path == key)
            out.push_back(NoteProperty(std::string(key) + kProblem, "Problem", error.Message));
}

PropertyNode ReferenceRow(std::string key, std::string label, std::string path, std::string tooltip)
{
    PropertyNode row;
    row.Key = std::move(key);
    row.Descriptor.Kind = PropertyKind::Asset;
    row.Descriptor.Label = std::move(label);
    row.Descriptor.Tooltip = std::move(tooltip);
    if (!path.empty())
        row.Descriptor.Verbs.push_back({ .Id = "open", .Label = "Open" });
    row.Value = std::move(path);
    return row;
}
} // namespace

std::vector<PropertyNode> DataEnvelopeProperties::Nodes() const
{
    const DataDocument* document = Shown ? Shown() : nullptr;
    if (document == nullptr || !document->Root().IsObject())
        return {};
    const JsonValue& root = document->Root();
    std::vector<PropertyNode> nodes;
    nodes.push_back(ReferenceRow(std::string(kExtends), "Extends", PathOf(root.Find("extends")),
                                 "The definition this one starts from; whatever is not set here comes from it."));
    AppendProblems(nodes, *document, kExtends);

    PropertyNode includes = GroupProperty(std::string(kIncludes), "Includes");
    includes.Descriptor.Tooltip = "Definitions whose values are added to this one, in order.";
    includes.Descriptor.Verbs.push_back({ .Id = "add", .Label = "Add", .Choices = Candidates(kIncludes) });
    if (const JsonValue* listed = root.Find("includes"); listed != nullptr && listed->IsArray())
        for (std::size_t index = 0; index < listed->AsArray().size(); ++index)
        {
            const std::string key = std::format("{}[{}]", kIncludes, index);
            includes.Children.push_back(
                ReferenceRow(key, std::format("Include {}", index + 1), PathOf(&listed->AsArray()[index]), {}));
            includes.Children.back().Descriptor.Verbs.push_back({ .Id = "delete", .Label = "Delete" });
            AppendProblems(includes.Children, *document, key);
        }
    nodes.push_back(std::move(includes));

    nodes.push_back(ToggleProperty(std::string(kAbstract), "Abstract", IsAbstractDataDefinition(root),
                                   "A template to extend or include; it is never loaded by itself."));
    AppendProblems(nodes, *document, kAbstract);
    return nodes;
}

bool DataEnvelopeProperties::BeginEdit(std::string_view key)
{
    DataDocument* document = Shown ? Shown() : nullptr;
    if (document == nullptr || (key != kExtends && key != kAbstract && !IncludeIndex(key)))
        return false;
    document->BeginEdit();
    return true;
}

JsonValue DataEnvelopeProperties::Reference(std::string_view path) const
{
    return AssetRefToJson(Documents.Store().Current(AssetRef{ .Path = std::string(path) }));
}

PropertyEditResult DataEnvelopeProperties::Rewrite(const std::function<bool(JsonValue::Object& envelope)>& rewrite,
                                                   DataEdit edit)
{
    DataDocument* document = Shown ? Shown() : nullptr;
    if (document == nullptr)
        return PropertyEditResult::Gone;
    JsonValue root = document->CopyRoot();
    if (!root.IsObject())
        return PropertyEditResult::Gone;
    if (!rewrite(root.AsObject()))
        return PropertyEditResult::Refused;
    Documents.ApplyEdit(*document, std::move(root), edit);
    return PropertyEditResult::Applied;
}

PropertyEditResult DataEnvelopeProperties::PreviewEdit(std::string_view key, std::string_view value)
{
    return Rewrite([&](JsonValue::Object& envelope) {
        if (key == kExtends)
        {
            value.empty() ? EraseMember(envelope, "extends") : SetMember(envelope, "extends", Reference(value));
            return true;
        }
        if (key == kAbstract)
        {
            const std::optional<bool> on = ParseToggle(value);
            if (!on.has_value())
                return false;
            *on ? SetMember(envelope, "abstract", JsonValue(true)) : EraseMember(envelope, "abstract");
            return true;
        }
        const std::optional<std::size_t> index = IncludeIndex(key);
        JsonValue* listed = MemberOf(envelope, "includes");
        if (!index || value.empty() || listed == nullptr || !listed->IsArray() || *index >= listed->AsArray().size())
            return false;
        listed->AsArray()[*index] = Reference(value);
        return true;
    }, DataEdit::Preview);
}

PropertyEditResult DataEnvelopeProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const PropertyEditResult previewed = PreviewEdit(key, value);
    DataDocument* document = Shown ? Shown() : nullptr;
    if (document == nullptr || !document->IsEditing())
        return previewed;
    if (previewed != PropertyEditResult::Applied)
    {
        CancelEdit(key);
        return previewed;
    }
    Documents.Store().CommitEdit(*document);
    return previewed;
}

void DataEnvelopeProperties::CancelEdit(std::string_view)
{
    if (DataDocument* document = Shown ? Shown() : nullptr)
        Documents.Store().CancelEdit(*document);
}

std::vector<std::string> DataEnvelopeProperties::Candidates(std::string_view key) const
{
    const DataDocument* document = Shown ? Shown() : nullptr;
    if (document == nullptr || (key != kExtends && key != kIncludes && !IncludeIndex(key)))
        return {};
    std::vector<std::string> paths = Documents.Store().DataAssetPaths(document->Subtype(), DataAbstractPaths::Include);
    std::erase(paths, document->VirtualPath());
    return paths;
}

PropertyEditResult DataEnvelopeProperties::Act(std::string_view key, std::string_view verb, std::string_view choice)
{
    if (verb == "open")
    {
        const DataDocument* document = Shown ? Shown() : nullptr;
        const std::optional<std::size_t> index = IncludeIndex(key);
        const JsonValue* listed = document != nullptr ? document->Root().Find("includes") : nullptr;
        const std::string path = key == kExtends && document != nullptr ? PathOf(document->Root().Find("extends"))
                               : index && listed != nullptr && listed->IsArray() && *index < listed->AsArray().size()
                                   ? PathOf(&listed->AsArray()[*index])
                                   : std::string{};
        std::string error;
        return path.empty() ? PropertyEditResult::Gone
             : Documents.OpenOrFocus(path, error) != nullptr ? PropertyEditResult::Applied
                                                             : PropertyEditResult::Refused;
    }
    return Rewrite([&](JsonValue::Object& envelope) {
        JsonValue* listed = MemberOf(envelope, "includes");
        if (verb == "add" && key == kIncludes && !choice.empty())
        {
            if (listed == nullptr || !listed->IsArray())
                SetMember(envelope, "includes", JsonValue(JsonValue::Array{}));
            MemberOf(envelope, "includes")->AsArray().push_back(Reference(choice));
            return true;
        }
        const std::optional<std::size_t> index = IncludeIndex(key);
        if (verb != "delete" || !index || listed == nullptr || !listed->IsArray() || *index >= listed->AsArray().size())
            return false;
        listed->AsArray().erase(listed->AsArray().begin() + static_cast<std::ptrdiff_t>(*index));
        if (listed->AsArray().empty())
            EraseMember(envelope, "includes");
        return true;
    }, DataEdit::Step);
}
