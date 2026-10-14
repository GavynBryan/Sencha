#include "authoring/AbilityArguments.h"

#include "authoring/AbilityElements.h"
#include "authoring/AbilityWorkbench.h"
#include "data/DataPath.h"
#include "data/JsonObjectEdit.h"
#include "properties/SettingProperties.h"
#include "vocabulary/VocabularyCatalog.h"

#include <abilities/AbilityValueNames.h>
#include <effects/EffectDefinitionData.h>

#include <array>
#include <format>
#include <optional>

namespace
{
constexpr std::string_view kArgument = "arg.";
constexpr std::string_view kSourceSuffix = ".source";
constexpr std::string_view kValueSuffix = ".value";

constexpr std::array<std::pair<AbilityArgumentSource, std::string_view>, 4> kSourceNames{ {
    { AbilityArgumentSource::Unset, "Unset" },
    { AbilityArgumentSource::Constant, "Constant" },
    { AbilityArgumentSource::Port, "Port" },
    { AbilityArgumentSource::Local, "Local" },
} };

std::string_view SourceName(AbilityArgumentSource source)
{
    for (const auto& [value, name] : kSourceNames)
        if (value == source)
            return name;
    return {};
}

const DataFieldSchema& Unwrapped(const DataFieldSchema& field)
{
    return field.Kind == DataFieldKind::Optional && field.Children.size() == 1 ? field.Children.front() : field;
}

bool Numeric(DataFieldKind kind)
{
    return kind == DataFieldKind::Int || kind == DataFieldKind::Float;
}

// Every local as it can be named, a hit's fields beside it.
std::vector<std::pair<std::string, JsonValue>> LocalChoices(const JsonValue& data)
{
    std::vector<std::pair<std::string, JsonValue>> choices;
    const JsonValue* locals = FindMember(data, "locals");
    if (locals == nullptr || !locals->IsArray())
        return choices;
    for (const JsonValue& local : locals->AsArray())
    {
        const std::string key = ReadMemberString(local, "key");
        const std::string name = ReadMemberString(local, "name").empty() ? key : ReadMemberString(local, "name");
        choices.emplace_back(name, JsonValue(JsonValue::Object{ { "local", JsonValue(key) } }));
        if (ReadMemberString(local, "type") == "hit")
            for (const AbilityNamed<AbilityHitField>& field : kAbilityHitFieldNames)
                choices.emplace_back(std::format("{}.{}", name, field.Name),
                                     JsonValue(JsonValue::Object{ { "local", JsonValue(key) },
                                                                  { "field", JsonValue(std::string(field.Name)) } }));
    }
    return choices;
}

std::string LocalText(const JsonValue& data, const JsonValue& argument)
{
    for (const auto& [name, value] : LocalChoices(data))
        if (ReadMemberString(value, "local") == ReadMemberString(argument, "local")
            && ReadMemberString(value, "field") == ReadMemberString(argument, "field"))
            return name;
    return ReadMemberString(argument, "local");
}

std::string ConstantText(const JsonValue& argument)
{
    const JsonValue* held = argument.IsObject() ? FindMember(argument, "const") : &argument;
    for (const std::string_view member : { "tag", "data", "asset" })
        if (const JsonValue* named = FindMember(argument, member); named != nullptr && named->IsString())
            return named->AsString();
    if (held == nullptr)
        return {};
    if (held->IsNumber())
        return NumberText(held->AsNumber());
    if (held->IsBool())
        return held->AsBool() ? "true" : "false";
    if (held->IsString())
        return held->AsString();
    if (held->IsArray())
    {
        std::string text;
        for (const JsonValue& component : held->AsArray())
            text += std::format("{}{}", text.empty() ? "" : ", ", component.IsNumber() ? component.AsNumber() : 0.0);
        return text;
    }
    return {};
}

// A constant of `field`'s kind as `text` writes it; none when it cannot be one.
std::optional<JsonValue> Constant(const DataFieldSchema& field, std::string_view text)
{
    const DataFieldKind kind = Unwrapped(field).Kind;
    const auto wrapped = [](std::string_view member, JsonValue value) {
        return JsonValue(JsonValue::Object{ { std::string(member), std::move(value) } });
    };
    if (Numeric(kind))
    {
        const std::optional<double> number = ParseNumber(text, 0.0, -1.0);
        return number ? std::optional(wrapped("const", JsonValue(*number))) : std::nullopt;
    }
    if (kind == DataFieldKind::Bool)
    {
        const std::optional<bool> on = ParseToggle(text);
        return on ? std::optional(wrapped("const", JsonValue(*on))) : std::nullopt;
    }
    if (kind == DataFieldKind::Vector)
    {
        const std::optional<std::vector<double>> parts = ParseVector(text, Unwrapped(field).VectorLength);
        if (!parts)
            return std::nullopt;
        JsonValue::Array components;
        for (const double part : *parts)
            components.emplace_back(part);
        return wrapped("const", JsonValue(std::move(components)));
    }
    if (kind == DataFieldKind::GameplayTag)
        return wrapped("tag", JsonValue(std::string(text)));
    if (kind == DataFieldKind::DataAssetRef)
        return wrapped("data", JsonValue(std::string(text)));
    if (kind == DataFieldKind::AssetRef)
        return wrapped("asset", JsonValue(std::string(text)));
    return wrapped("const", JsonValue(std::string(text)));
}

JsonValue DefaultConstant(const DataFieldSchema& field)
{
    const DataFieldKind kind = Unwrapped(field).Kind;
    if (Numeric(kind))
        return *Constant(field, "0");
    if (kind == DataFieldKind::Bool)
        return *Constant(field, "false");
    if (kind == DataFieldKind::Vector)
        return *Constant(field, "0, 0, 0");
    return *Constant(field, "");
}

JsonValue DefaultPort(const DataFieldSchema& field)
{
    const DataFieldKind kind = Unwrapped(field).Kind;
    const std::string_view port = kind == DataFieldKind::Vector ? "aim.origin"
                                : Numeric(kind)                 ? "phase.progress"
                                                                : "actor";
    return JsonValue(JsonValue::Object{ { "port", JsonValue(std::string(port)) } });
}

struct ArgumentKey
{
    std::string Name;
    bool Source = false;
};

std::optional<ArgumentKey> ParseKey(std::string_view key)
{
    if (!key.starts_with(kArgument))
        return std::nullopt;
    key.remove_prefix(kArgument.size());
    if (key.ends_with(kSourceSuffix))
        return ArgumentKey{ std::string(key.substr(0, key.size() - kSourceSuffix.size())), true };
    if (key.ends_with(kValueSuffix))
        return ArgumentKey{ std::string(key.substr(0, key.size() - kValueSuffix.size())), false };
    return std::nullopt;
}

std::vector<std::string_view> Names(const std::vector<AbilityArgumentSource>& sources)
{
    std::vector<std::string_view> names;
    for (const AbilityArgumentSource source : sources)
        names.push_back(SourceName(source));
    return names;
}
} // namespace

const DataFieldSchema* AbilityOpContract(const JsonValue& op, const VocabularyCatalog& names)
{
    const std::string lease = ReadMemberString(op, "lease");
    const std::string verb = lease.empty() ? ReadMemberString(op, "verb") : lease;
    if (!verb.empty() && (!lease.empty() || ReadMemberString(op, "op") == "invoke"))
    {
        const VerbDefinition* definition = names.Verbs().Get(names.Verbs().Find(verb));
        return definition != nullptr ? &definition->Arguments : nullptr;
    }
    if (ReadMemberString(op, "op") == "query")
    {
        const AuthoredQueryDefinition* definition =
            names.Queries().Get(names.Queries().Find(ReadMemberString(op, "query")));
        return definition != nullptr ? &definition->Arguments : nullptr;
    }
    return nullptr;
}

std::vector<AbilityArgumentSource> AbilityArgumentSources(const DataFieldSchema& field)
{
    std::vector<AbilityArgumentSource> sources;
    if (!field.Required)
        sources.push_back(AbilityArgumentSource::Unset);
    const DataFieldKind kind = Unwrapped(field).Kind;
    if (kind != DataFieldKind::Entity)
        sources.push_back(AbilityArgumentSource::Constant);
    if (kind == DataFieldKind::Entity || kind == DataFieldKind::Vector || kind == DataFieldKind::Bool || Numeric(kind))
    {
        sources.push_back(AbilityArgumentSource::Port);
        sources.push_back(AbilityArgumentSource::Local);
    }
    return sources;
}

AbilityArgumentSource AbilityArgumentSourceOf(const JsonValue* argument)
{
    if (argument == nullptr || argument->IsNull())
        return AbilityArgumentSource::Unset;
    if (FindMember(*argument, "port") != nullptr)
        return AbilityArgumentSource::Port;
    if (FindMember(*argument, "local") != nullptr)
        return AbilityArgumentSource::Local;
    return AbilityArgumentSource::Constant;
}

AbilityArgumentProperties::AbilityArgumentProperties(AbilityWorkbench& workbench,
                                                     std::function<void(std::string)> createEffect)
    : Workbench(workbench)
    , CreateEffect(std::move(createEffect))
{
}

std::vector<PropertyNode> AbilityArgumentProperties::Nodes() const
{
    const std::optional<AbilityElement> op = Workbench.SelectedElement();
    const JsonValue* data = Workbench.AbilityData();
    const JsonValue* value = op && op->Kind == AbilityElementKind::Op && data != nullptr
                               ? FindDataValue(*data, op->Path)
                               : nullptr;
    const DataFieldSchema* contract = value != nullptr ? AbilityOpContract(*value, Workbench.Names()) : nullptr;
    if (contract == nullptr || contract->Children.empty())
        return {};
    const JsonValue* arguments = FindMember(*value, "arguments");
    PropertyNode group = GroupProperty("arguments", "Arguments");
    for (const DataFieldSchema& field : contract->Children)
    {
        const JsonValue* argument = arguments != nullptr ? FindMember(*arguments, field.Key) : nullptr;
        const AbilityArgumentSource source = AbilityArgumentSourceOf(argument);
        const std::vector<AbilityArgumentSource> sources = AbilityArgumentSources(field);
        const std::vector<std::string_view> names = Names(sources);
        const auto at = std::ranges::find(sources, source);
        const std::string key = std::format("{}{}", kArgument, field.Key);
        PropertyNode row = GroupProperty(key, field.DisplayName.empty() ? field.Key : field.DisplayName);
        row.Descriptor.Tooltip = field.Summary;
        row.Children.push_back(ChoiceProperty(key + std::string(kSourceSuffix), "From", names,
                                              at != sources.end() ? static_cast<std::size_t>(at - sources.begin()) : 0));
        const std::string valueKey = key + std::string(kValueSuffix);
        if (source == AbilityArgumentSource::Port)
        {
            std::vector<std::string_view> ports;
            std::size_t current = 0;
            for (const AbilityNamed<AbilityPort>& port : kAbilityPortNames)
            {
                if (port.Name == ReadMemberString(*argument, "port"))
                    current = ports.size();
                ports.push_back(port.Name);
            }
            row.Children.push_back(ChoiceProperty(valueKey, "Port", ports, current));
        }
        else if (source == AbilityArgumentSource::Local)
        {
            const std::vector<std::pair<std::string, JsonValue>> locals = LocalChoices(*data);
            std::vector<std::string_view> options;
            for (const auto& [name, unused] : locals)
                options.push_back(name);
            const std::string current = LocalText(*data, *argument);
            const auto found = std::ranges::find(options, std::string_view(current));
            if (found == options.end())
                options.push_back(current);
            row.Children.push_back(ChoiceProperty(valueKey, "Local", options,
                                                  static_cast<std::size_t>(std::ranges::find(options, std::string_view(current))
                                                                           - options.begin())));
        }
        else if (source == AbilityArgumentSource::Constant)
        {
            PropertyNode constant = TextProperty(valueKey, "Value", ConstantText(*argument));
            const DataFieldSchema& shown = Unwrapped(field);
            if (shown.Kind == DataFieldKind::DataAssetRef)
            {
                constant.Descriptor.Kind = PropertyKind::Asset;
                constant.Descriptor.Asset = AssetType::Data;
                if (shown.Reference.DataSubtype == kEffectDefinitionType)
                    constant.Descriptor.Verbs.push_back({ .Id = "new", .Label = "New effect\xE2\x80\xA6" });
            }
            row.Children.push_back(std::move(constant));
        }
        group.Children.push_back(std::move(row));
    }
    return { std::move(group) };
}

PropertyEditResult AbilityArgumentProperties::CommitEdit(std::string_view key, std::string_view value)
{
    const std::optional<ArgumentKey> parsed = ParseKey(key);
    const std::optional<AbilityElement> op = Workbench.SelectedElement();
    const JsonValue* data = Workbench.AbilityData();
    const JsonValue* held = op && data != nullptr ? FindDataValue(*data, op->Path) : nullptr;
    const DataFieldSchema* contract = held != nullptr ? AbilityOpContract(*held, Workbench.Names()) : nullptr;
    if (!parsed || contract == nullptr)
        return PropertyEditResult::Gone;
    const auto field = std::ranges::find(contract->Children, parsed->Name, &DataFieldSchema::Key);
    if (field == contract->Children.end())
        return PropertyEditResult::Gone;
    const JsonValue* arguments = FindMember(*held, "arguments");
    const JsonValue* argument = arguments != nullptr ? FindMember(*arguments, parsed->Name) : nullptr;

    std::optional<JsonValue> written;
    if (parsed->Source)
    {
        const auto source = std::ranges::find(kSourceNames, value, &std::pair<AbilityArgumentSource, std::string_view>::second);
        if (source == kSourceNames.end())
            return PropertyEditResult::Refused;
        if (source->first == AbilityArgumentSourceOf(argument))
            return PropertyEditResult::Unchanged;
        switch (source->first)
        {
        case AbilityArgumentSource::Unset: written = JsonValue(); break;
        case AbilityArgumentSource::Constant: written = DefaultConstant(*field); break;
        case AbilityArgumentSource::Port: written = DefaultPort(*field); break;
        case AbilityArgumentSource::Local:
        {
            const std::vector<std::pair<std::string, JsonValue>> locals = LocalChoices(*data);
            written = locals.empty() ? JsonValue(JsonValue::Object{ { "local", JsonValue("") } }) : locals.front().second;
            break;
        }
        }
    }
    else
    {
        switch (AbilityArgumentSourceOf(argument))
        {
        case AbilityArgumentSource::Unset: return PropertyEditResult::Gone;
        case AbilityArgumentSource::Constant: written = Constant(*field, value); break;
        case AbilityArgumentSource::Port:
            if (FindAbilityNamed(kAbilityPortNames, value))
                written = JsonValue(JsonValue::Object{ { "port", JsonValue(std::string(value)) } });
            break;
        case AbilityArgumentSource::Local:
            for (const auto& [name, local] : LocalChoices(*data))
                if (name == value)
                    written = local;
            break;
        }
    }
    if (!written)
        return PropertyEditResult::Refused;
    const std::string path = op->Path;
    const bool applied = Workbench.Edit([&](JsonValue& edited) {
        JsonValue* target = FindDataValue(edited, path);
        if (target == nullptr)
            return false;
        if (written->IsNull())
        {
            if (JsonValue* list = FindMember(*target, "arguments"))
                EraseMember(*list, parsed->Name);
            return true;
        }
        if (FindMember(*target, "arguments") == nullptr)
            SetMember(*target, "arguments", JsonValue(JsonValue::Object{}));
        SetMember(*FindMember(*target, "arguments"), parsed->Name, *written);
        return true;
    });
    return applied ? PropertyEditResult::Applied : PropertyEditResult::Refused;
}

std::vector<std::string> AbilityArgumentProperties::Candidates(std::string_view key) const
{
    const std::optional<ArgumentKey> parsed = ParseKey(key);
    const std::optional<AbilityElement> op = Workbench.SelectedElement();
    const JsonValue* data = Workbench.AbilityData();
    const JsonValue* held = op && data != nullptr ? FindDataValue(*data, op->Path) : nullptr;
    const DataFieldSchema* contract = held != nullptr ? AbilityOpContract(*held, Workbench.Names()) : nullptr;
    if (!parsed || parsed->Source || contract == nullptr)
        return {};
    const auto field = std::ranges::find(contract->Children, parsed->Name, &DataFieldSchema::Key);
    if (field == contract->Children.end() || Unwrapped(*field).Kind != DataFieldKind::DataAssetRef)
        return {};
    std::vector<std::string> paths;
    for (const ProjectFile& file : Workbench.FilesOf(Unwrapped(*field).Reference.DataSubtype))
        paths.push_back(file.AssetPath);
    return paths;
}

PropertyEditResult AbilityArgumentProperties::Act(std::string_view key, std::string_view verb, std::string_view)
{
    const std::optional<ArgumentKey> parsed = ParseKey(key);
    if (!parsed || parsed->Source || verb != "new" || !CreateEffect)
        return PropertyEditResult::Gone;
    CreateEffect(parsed->Name);
    return PropertyEditResult::Applied;
}
