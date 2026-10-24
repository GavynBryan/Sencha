#include "vocabulary/VerbSequenceRows.h"

#include "data/JsonObjectEdit.h"
#include "properties/PropertyTree.h"
#include "properties/SettingProperties.h"
#include "vocabulary/AuthoredConstantText.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

namespace
{
constexpr std::string_view kWorldFrame = "World";
constexpr std::string_view kEntityCondition = "Entity has\xE2\x80\xA6";
constexpr std::array<std::string_view, 2> kFailurePolicies = { "Continue", "Stop" };

enum class ArgumentSource : std::uint8_t
{
    Unset,
    Constant,
    Port,
    Entity,
};

constexpr std::array<std::pair<ArgumentSource, std::string_view>, 4> kSourceNames{ {
    { ArgumentSource::Unset, "Unset" },
    { ArgumentSource::Constant, "Constant" },
    { ArgumentSource::Port, "Port" },
    { ArgumentSource::Entity, "Entity" },
} };

std::string_view SourceName(ArgumentSource source)
{
    for (const auto& [value, name] : kSourceNames)
        if (value == source)
            return name;
    return {};
}

ArgumentSource SourceOf(const JsonValue* argument)
{
    if (argument == nullptr || argument->IsNull())
        return ArgumentSource::Unset;
    if (FindMember(*argument, "port") != nullptr)
        return ArgumentSource::Port;
    if (FindMember(*argument, "entity") != nullptr)
        return ArgumentSource::Entity;
    return ArgumentSource::Constant;
}

bool IsVector3(const DataFieldSchema& field)
{
    return field.Kind == DataFieldKind::Vector && field.VectorLength == 3;
}

// The ports a field of this kind may read.
std::vector<std::string_view> PortsFor(const DataFieldSchema& field, std::span<const SequencePort> ports)
{
    const DataFieldSchema& shown = UnwrappedField(field);
    std::vector<std::string_view> names;
    for (const SequencePort& port : ports)
        if ((port.Kind == SequencePortKind::Entity && shown.Kind == DataFieldKind::Entity)
            || (port.Kind == SequencePortKind::Vector && IsVector3(shown)))
            names.push_back(port.Name);
    return names;
}

std::vector<std::string_view> EntityPorts(std::span<const SequencePort> ports)
{
    std::vector<std::string_view> names;
    for (const SequencePort& port : ports)
        if (port.Kind == SequencePortKind::Entity)
            names.push_back(port.Name);
    return names;
}

std::vector<ArgumentSource> SourcesFor(const DataFieldSchema& field, std::span<const SequencePort> ports,
                                       bool absent)
{
    const DataFieldSchema& shown = UnwrappedField(field);
    std::vector<ArgumentSource> sources;
    if (!field.Required || absent)
        sources.push_back(ArgumentSource::Unset);
    if (shown.Kind != DataFieldKind::Entity)
        sources.push_back(ArgumentSource::Constant);
    if (!PortsFor(field, ports).empty())
        sources.push_back(ArgumentSource::Port);
    if (shown.Kind == DataFieldKind::Entity)
        sources.push_back(ArgumentSource::Entity);
    return sources;
}

std::size_t IndexOf(std::span<const std::string_view> options, std::string_view value)
{
    const auto found = std::ranges::find(options, value);
    return found != options.end() ? static_cast<std::size_t>(found - options.begin()) : 0;
}

std::string Joined(const JsonValue* names)
{
    std::string text;
    if (names == nullptr || !names->IsArray())
        return text;
    for (const JsonValue& name : names->AsArray())
        if (name.IsString())
            text += (text.empty() ? "" : ", ") + name.AsString();
    return text;
}

JsonValue Names(std::string_view text)
{
    JsonValue::Array names;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t comma = std::min(text.find(',', start), text.size());
        std::string_view name = text.substr(start, comma - start);
        while (!name.empty() && name.front() == ' ')
            name.remove_prefix(1);
        while (!name.empty() && name.back() == ' ')
            name.remove_suffix(1);
        if (!name.empty())
            names.emplace_back(std::string(name));
        start = comma + 1;
    }
    return JsonValue(std::move(names));
}

JsonValue Port(std::string_view name)
{
    return JsonValue(JsonValue::Object{ { "port", JsonValue(std::string(name)) } });
}

const VerbDefinition* VerbNamed(const VerbSequenceVocabulary& vocabulary, std::string_view name)
{
    return vocabulary.Verbs != nullptr ? vocabulary.Verbs->Get(vocabulary.Verbs->Find(name)) : nullptr;
}

const AuthoredQueryDefinition* QueryNamed(const VerbSequenceVocabulary& vocabulary, std::string_view name)
{
    return vocabulary.Queries != nullptr ? vocabulary.Queries->Get(vocabulary.Queries->Find(name)) : nullptr;
}

std::vector<std::string> InstantVerbs(const VerbSequenceVocabulary& vocabulary, std::vector<std::string>* notes)
{
    std::vector<std::string> names;
    if (vocabulary.Verbs == nullptr)
        return names;
    for (const VerbId id : vocabulary.Verbs->Live())
    {
        const VerbDefinition& verb = *vocabulary.Verbs->Get(id);
        if (verb.Kind != VerbKind::Instant)
            continue;
        names.push_back(verb.Name);
        if (notes != nullptr)
            notes->push_back(verb.Description);
    }
    return names;
}

std::vector<std::string> TruthQueries(const VerbSequenceVocabulary& vocabulary)
{
    std::vector<std::string> names;
    if (vocabulary.Queries == nullptr)
        return names;
    for (const AuthoredQueryId id : vocabulary.Queries->Live())
    {
        const AuthoredQueryDefinition& query = *vocabulary.Queries->Get(id);
        if (UnwrappedField(query.Result).Kind == DataFieldKind::Bool)
            names.push_back(query.Name);
    }
    return names;
}

std::vector<std::string_view> Views(const std::vector<std::string>& strings)
{
    return { strings.begin(), strings.end() };
}

// The arguments of one call, laid out from what it declares.
void ArgumentRows(const std::string& prefix, const DataFieldSchema& contract, const JsonValue* arguments,
                  std::span<const SequencePort> ports, bool entityReferences, PropertyNode& into)
{
    for (const DataFieldSchema& field : contract.Children)
    {
        const JsonValue* argument = arguments != nullptr ? FindMember(*arguments, field.Key) : nullptr;
        const ArgumentSource source = SourceOf(argument);
        std::vector<ArgumentSource> sources = SourcesFor(field, ports, argument == nullptr);
        if (!entityReferences)
            std::erase(sources, ArgumentSource::Entity);
        std::vector<std::string_view> names;
        for (const ArgumentSource option : sources)
            names.push_back(SourceName(option));
        const std::string key = std::format("{}.arg.{}", prefix, field.Key);
        PropertyNode row = GroupProperty(key, field.DisplayName.empty() ? field.Key : field.DisplayName);
        row.Descriptor.Tooltip = field.Summary;
        row.Children.push_back(ChoiceProperty(key + ".source", "From", names, IndexOf(names, SourceName(source))));
        const DataFieldSchema& shown = UnwrappedField(field);
        switch (source)
        {
        case ArgumentSource::Unset:
            break;
        case ArgumentSource::Port:
        {
            const std::vector<std::string_view> options = PortsFor(field, ports);
            row.Children.push_back(
                ChoiceProperty(key + ".value", "Port", options, IndexOf(options, ReadMemberString(*argument, "port"))));
            break;
        }
        case ArgumentSource::Entity:
            row.Children.push_back(TextProperty(key + ".value", "Identity", ReadMemberString(*argument, "entity"),
                                                "The entity's persistent identity, sixteen hex digits."));
            break;
        case ArgumentSource::Constant:
        {
            PropertyNode constant = TextProperty(key + ".value", "Value", AuthoredConstantText(*argument));
            if (shown.Kind == DataFieldKind::DataAssetRef)
            {
                constant.Descriptor.Kind = PropertyKind::Asset;
                constant.Descriptor.Asset = AssetType::Data;
            }
            row.Children.push_back(std::move(constant));
            if (IsVector3(shown))
            {
                std::vector<std::string_view> frames{ kWorldFrame };
                for (const std::string_view port : EntityPorts(ports))
                    frames.push_back(port);
                row.Children.push_back(ChoiceProperty(key + ".frame", "In the frame of", frames,
                                                      IndexOf(frames, ReadMemberString(*argument, "frame")),
                                                      "A direction given in an entity's frame turns with it."));
            }
            break;
        }
        }
        into.Children.push_back(std::move(row));
    }
}

PropertyNode Movable(PropertyNode row, std::size_t index, std::size_t count)
{
    if (index > 0)
        row.Descriptor.Verbs.push_back({ .Id = "up", .Label = "Up" });
    if (index + 1 < count)
        row.Descriptor.Verbs.push_back({ .Id = "down", .Label = "Down" });
    row.Descriptor.Verbs.push_back({ .Id = "remove", .Label = "Remove", .Removes = true });
    return row;
}

PropertyNode ConditionRow(const JsonValue& condition, std::size_t index, std::size_t count,
                          const VerbSequenceVocabulary& vocabulary)
{
    const std::string prefix = std::format("c{}", index);
    if (FindMember(condition, "query") != nullptr)
    {
        const std::string name = ReadMemberString(condition, "query");
        const AuthoredQueryDefinition* query = QueryNamed(vocabulary, name);
        PropertyNode row = GroupProperty(prefix, query != nullptr && !query->DisplayName.empty()
                                                     ? query->DisplayName
                                                     : name);
        std::vector<std::string> queries = TruthQueries(vocabulary);
        if (std::ranges::find(queries, name) == queries.end())
            queries.push_back(name);
        const std::vector<std::string_view> options = Views(queries);
        row.Children.push_back(ChoiceProperty(prefix + ".query", "Query", options, IndexOf(options, name)));
        const JsonValue* expect = FindMember(condition, "expect");
        row.Children.push_back(ToggleProperty(prefix + ".expect", "Holds when true",
                                              expect == nullptr || !expect->IsBool() || expect->AsBool()));
        if (query != nullptr)
            ArgumentRows(prefix, query->Arguments, FindMember(condition, "arguments"), vocabulary.Ports, false, row);
        return Movable(std::move(row), index, count);
    }

    const std::string port = ReadMemberString(condition, "entity");
    PropertyNode row = GroupProperty(prefix, std::format("{} has", port));
    const std::vector<std::string_view> ports = EntityPorts(vocabulary.Ports);
    row.Children.push_back(ChoiceProperty(prefix + ".entity", "Entity", ports, IndexOf(ports, port)));
    row.Children.push_back(TextProperty(prefix + ".has", "Components", Joined(FindMember(condition, "has")),
                                        "Component identities, separated by commas."));
    const JsonValue* tags = FindMember(condition, "tags");
    for (const std::string_view clause : { "all", "any", "none" })
        row.Children.push_back(TextProperty(std::format("{}.tags.{}", prefix, clause),
                                            std::format("Tags: {}", clause),
                                            Joined(tags != nullptr ? FindMember(*tags, clause) : nullptr),
                                            "Gameplay tag names, separated by commas."));
    return Movable(std::move(row), index, count);
}

PropertyNode CommandRow(const JsonValue& command, std::size_t index, std::size_t count,
                        const VerbSequenceVocabulary& vocabulary)
{
    const std::string prefix = std::format("k{}", index);
    const std::string name = ReadMemberString(command, "verb");
    const VerbDefinition* verb = VerbNamed(vocabulary, name);
    PropertyNode row = GroupProperty(prefix, std::format("{}. {}", index + 1,
                                                         verb != nullptr && !verb->DisplayName.empty()
                                                             ? verb->DisplayName
                                                             : name));
    std::vector<std::string> verbs = InstantVerbs(vocabulary, nullptr);
    if (std::ranges::find(verbs, name) == verbs.end())
        verbs.push_back(name);
    const std::vector<std::string_view> options = Views(verbs);
    row.Children.push_back(ChoiceProperty(prefix + ".verb", "Verb", options, IndexOf(options, name),
                                          verb != nullptr ? verb->Description : std::string("Not declared here.")));
    row.Children.push_back(ChoiceProperty(prefix + ".on_failure", "If it fails", kFailurePolicies,
                                          ReadMemberString(command, "on_failure") == "stop" ? 1 : 0,
                                          "Continue runs the commands after it; Stop ends this activation."));
    if (verb != nullptr)
        ArgumentRows(prefix, verb->Arguments, FindMember(command, "arguments"), vocabulary.Ports, true, row);
    return Movable(std::move(row), index, count);
}

struct EntryKey
{
    std::string_view List;
    std::size_t Index = 0;
    std::string_view Rest;
};

std::optional<EntryKey> ParseEntryKey(std::string_view key)
{
    for (const auto& [prefix, list] : { std::pair<std::string_view, std::string_view>{ "c", "conditions" },
                                        std::pair<std::string_view, std::string_view>{ "k", "commands" } })
        if (const std::optional<PropertyKeyIndex> parsed = ParsePropertyKeyIndex(key, prefix))
            return EntryKey{ list, parsed->Index, parsed->Rest };
    return std::nullopt;
}

JsonValue& ListOf(JsonValue& sequence, std::string_view list)
{
    if (!sequence.IsObject())
        sequence = JsonValue(JsonValue::Object{});
    if (JsonValue* found = FindMember(sequence, list); found == nullptr || !found->IsArray())
        SetMember(sequence, list, JsonValue(JsonValue::Array{}));
    return *FindMember(sequence, list);
}

JsonValue& ArgumentsOf(JsonValue& entry)
{
    if (JsonValue* found = FindMember(entry, "arguments"); found == nullptr || !found->IsObject())
        SetMember(entry, "arguments", JsonValue(JsonValue::Object{}));
    return *FindMember(entry, "arguments");
}

const DataFieldSchema* ContractOf(const JsonValue& entry, const VerbSequenceVocabulary& vocabulary)
{
    if (FindMember(entry, "verb") != nullptr)
    {
        const VerbDefinition* verb = VerbNamed(vocabulary, ReadMemberString(entry, "verb"));
        return verb != nullptr ? &verb->Arguments : nullptr;
    }
    const AuthoredQueryDefinition* query = QueryNamed(vocabulary, ReadMemberString(entry, "query"));
    return query != nullptr ? &query->Arguments : nullptr;
}

// A command's arguments start where a designer most often wants them: an
// entity from the first port that can be one, anything required at its default.
JsonValue StartingArguments(const DataFieldSchema& contract, std::span<const SequencePort> ports)
{
    JsonValue::Object arguments;
    for (const DataFieldSchema& field : contract.Children)
    {
        if (!field.Required)
            continue;
        const std::vector<std::string_view> fitting = PortsFor(field, ports);
        if (UnwrappedField(field).Kind == DataFieldKind::Entity && !fitting.empty())
            arguments.emplace_back(field.Key, Port(fitting.front()));
        else if (UnwrappedField(field).Kind != DataFieldKind::Entity)
            arguments.emplace_back(field.Key, DefaultAuthoredConstant(field));
    }
    return JsonValue(std::move(arguments));
}

std::optional<JsonValue> ArgumentValue(const DataFieldSchema& field, const JsonValue* current, std::string_view rest,
                                       std::string_view value, std::span<const SequencePort> ports)
{
    if (rest == "source")
    {
        const auto source = std::ranges::find(kSourceNames, value, &std::pair<ArgumentSource, std::string_view>::second);
        if (source == kSourceNames.end())
            return std::nullopt;
        switch (source->first)
        {
        case ArgumentSource::Unset: return JsonValue();
        case ArgumentSource::Constant: return DefaultAuthoredConstant(field);
        case ArgumentSource::Entity: return JsonValue(JsonValue::Object{ { "entity", JsonValue(std::string()) } });
        case ArgumentSource::Port:
        {
            const std::vector<std::string_view> fitting = PortsFor(field, ports);
            if (fitting.empty())
                return std::nullopt;
            return Port(fitting.front());
        }
        }
        return std::nullopt;
    }
    // A value typed for an argument nothing sets yet sets it as a constant.
    if (current == nullptr)
        return rest == "value" && UnwrappedField(field).Kind != DataFieldKind::Entity
            ? AuthoredConstantFromText(field, value)
            : std::nullopt;
    if (rest == "frame")
    {
        JsonValue framed = *current;
        if (value == kWorldFrame)
            EraseMember(framed, "frame");
        else if (const std::vector<std::string_view> frames = EntityPorts(ports);
                 std::ranges::find(frames, value) != frames.end())
            SetMember(framed, "frame", JsonValue(std::string(value)));
        else
            return std::nullopt;
        return framed;
    }
    if (rest != "value")
        return std::nullopt;
    switch (SourceOf(current))
    {
    case ArgumentSource::Unset: return std::nullopt;
    case ArgumentSource::Port:
    {
        const std::vector<std::string_view> fitting = PortsFor(field, ports);
        if (std::ranges::find(fitting, value) == fitting.end())
            return std::nullopt;
        return Port(value);
    }
    case ArgumentSource::Entity: return JsonValue(JsonValue::Object{ { "entity", JsonValue(std::string(value)) } });
    case ArgumentSource::Constant:
    {
        std::optional<JsonValue> constant = AuthoredConstantFromText(field, value);
        if (constant && current->IsObject())
            if (const JsonValue* frame = FindMember(*current, "frame"))
                SetMember(*constant, "frame", *frame);
        return constant;
    }
    }
    return std::nullopt;
}
} // namespace

std::vector<PropertyNode> VerbSequenceRows(const JsonValue& sequence, const VerbSequenceVocabulary& vocabulary,
                                           std::span<const std::string> problems)
{
    const JsonValue* conditions = FindMember(sequence, "conditions");
    const JsonValue* commands = FindMember(sequence, "commands");

    PropertyNode when = GroupProperty("conditions", "Only when");
    when.Descriptor.Tooltip = "Every condition must hold for the commands to run.";
    PropertyVerb addCondition{ .Id = "add", .Label = "Add Condition" };
    addCondition.Choices.emplace_back(kEntityCondition);
    addCondition.ChoiceNotes.emplace_back("An entity port carries components and gameplay tags.");
    for (const std::string& query : TruthQueries(vocabulary))
    {
        addCondition.Choices.push_back(query);
        addCondition.ChoiceNotes.emplace_back();
    }
    when.Descriptor.Verbs.push_back(std::move(addCondition));
    if (conditions != nullptr && conditions->IsArray())
        for (std::size_t i = 0; i < conditions->AsArray().size(); ++i)
            when.Children.push_back(ConditionRow(conditions->AsArray()[i], i, conditions->AsArray().size(), vocabulary));

    PropertyNode then = GroupProperty("commands", "Then");
    then.Descriptor.Tooltip = "Run in order, each a verb.";
    PropertyVerb addCommand{ .Id = "add", .Label = "Add Command" };
    addCommand.Choices = InstantVerbs(vocabulary, &addCommand.ChoiceNotes);
    then.Descriptor.Verbs.push_back(std::move(addCommand));
    if (commands != nullptr && commands->IsArray())
        for (std::size_t i = 0; i < commands->AsArray().size(); ++i)
            then.Children.push_back(CommandRow(commands->AsArray()[i], i, commands->AsArray().size(), vocabulary));

    std::vector<PropertyNode> rows{ std::move(when), std::move(then) };
    if (!problems.empty())
    {
        PropertyNode notes = GroupProperty("problems", std::format("Problems ({})", problems.size()));
        for (std::size_t i = 0; i < problems.size(); ++i)
            notes.Children.push_back(NoteProperty(std::format("problem{}", i), "", problems[i]));
        rows.push_back(std::move(notes));
    }
    return rows;
}

std::optional<JsonValue> SetVerbSequenceValue(const JsonValue& sequence, const VerbSequenceVocabulary& vocabulary,
                                              std::string_view key, std::string_view value)
{
    const std::optional<EntryKey> entry = ParseEntryKey(key);
    if (!entry)
        return std::nullopt;
    JsonValue edited = sequence;
    JsonValue& list = ListOf(edited, entry->List);
    if (entry->Index >= list.AsArray().size())
        return std::nullopt;
    JsonValue& target = list.AsArray()[entry->Index];
    const std::string_view rest = entry->Rest;

    if (rest == "verb")
    {
        const VerbDefinition* verb = VerbNamed(vocabulary, value);
        if (verb == nullptr)
            return std::nullopt;
        SetMember(target, "verb", JsonValue(std::string(value)));
        // What the old verb was given and the new one also takes is kept.
        JsonValue& arguments = ArgumentsOf(target);
        std::erase_if(arguments.AsObject(), [&](const auto& member) {
            return std::ranges::none_of(verb->Arguments.Children,
                                        [&](const DataFieldSchema& field) { return field.Key == member.first; });
        });
        const JsonValue starting = StartingArguments(verb->Arguments, vocabulary.Ports);
        for (const auto& [name, initial] : starting.AsObject())
            if (FindMember(arguments, name) == nullptr)
                SetMember(arguments, name, initial);
        return edited;
    }
    if (rest == "on_failure")
    {
        if (value == kFailurePolicies[1])
            SetMember(target, "on_failure", JsonValue(std::string("stop")));
        else if (value == kFailurePolicies[0])
            EraseMember(target, "on_failure");
        else
            return std::nullopt;
        return edited;
    }
    if (rest == "entity")
    {
        const std::vector<std::string_view> ports = EntityPorts(vocabulary.Ports);
        if (std::ranges::find(ports, value) == ports.end())
            return std::nullopt;
        SetMember(target, "entity", JsonValue(std::string(value)));
        return edited;
    }
    if (rest == "has")
    {
        SetMember(target, "has", Names(value));
        return edited;
    }
    if (rest.starts_with("tags."))
    {
        const std::string_view clause = rest.substr(5);
        if (clause != "all" && clause != "any" && clause != "none")
            return std::nullopt;
        if (FindMember(target, "tags") == nullptr)
            SetMember(target, "tags", JsonValue(JsonValue::Object{}));
        SetMember(*FindMember(target, "tags"), clause, Names(value));
        return edited;
    }
    if (rest == "query")
    {
        const AuthoredQueryDefinition* query = QueryNamed(vocabulary, value);
        if (query == nullptr)
            return std::nullopt;
        SetMember(target, "query", JsonValue(std::string(value)));
        SetMember(target, "arguments", JsonValue(JsonValue::Object{}));
        return edited;
    }
    if (rest == "expect")
    {
        const std::optional<bool> on = ParseToggle(value);
        if (!on)
            return std::nullopt;
        SetMember(target, "expect", JsonValue(*on));
        return edited;
    }
    if (rest.starts_with("arg."))
    {
        const std::string_view argument = rest.substr(4);
        const std::size_t dot = argument.rfind('.');
        if (dot == std::string_view::npos)
            return std::nullopt;
        const std::string_view name = argument.substr(0, dot);
        const DataFieldSchema* contract = ContractOf(target, vocabulary);
        const auto field = contract != nullptr
            ? std::ranges::find(contract->Children, name, &DataFieldSchema::Key)
            : std::vector<DataFieldSchema>::const_iterator{};
        if (contract == nullptr || field == contract->Children.end())
            return std::nullopt;
        JsonValue& arguments = ArgumentsOf(target);
        const std::optional<JsonValue> written =
            ArgumentValue(*field, FindMember(arguments, name), argument.substr(dot + 1), value, vocabulary.Ports);
        if (!written)
            return std::nullopt;
        if (written->IsNull())
            EraseMember(arguments, name);
        else
            SetMember(arguments, name, *written);
        return edited;
    }
    return std::nullopt;
}

std::optional<JsonValue> ActOnVerbSequence(const JsonValue& sequence, const VerbSequenceVocabulary& vocabulary,
                                           std::string_view key, std::string_view verb, std::string_view choice)
{
    JsonValue edited = sequence;
    if (verb == "add" && (key == "conditions" || key == "commands"))
    {
        JsonValue::Array& list = ListOf(edited, key).AsArray();
        if (key == "commands")
        {
            const VerbDefinition* added = VerbNamed(vocabulary, choice);
            if (added == nullptr || added->Kind != VerbKind::Instant)
                return std::nullopt;
            list.push_back(JsonValue(JsonValue::Object{
                { "verb", JsonValue(std::string(choice)) },
                { "arguments", StartingArguments(added->Arguments, vocabulary.Ports) } }));
            return edited;
        }
        if (choice == kEntityCondition)
        {
            const std::vector<std::string_view> ports = EntityPorts(vocabulary.Ports);
            if (ports.empty())
                return std::nullopt;
            list.push_back(JsonValue(JsonValue::Object{ { "entity", JsonValue(std::string(ports.front())) },
                                                        { "has", JsonValue(JsonValue::Array{}) } }));
            return edited;
        }
        const AuthoredQueryDefinition* query = QueryNamed(vocabulary, choice);
        if (query == nullptr)
            return std::nullopt;
        list.push_back(JsonValue(JsonValue::Object{ { "query", JsonValue(std::string(choice)) },
                                                    { "arguments", StartingArguments(query->Arguments,
                                                                                     vocabulary.Ports) } }));
        return edited;
    }

    const std::optional<EntryKey> entry = ParseEntryKey(key);
    if (!entry || !entry->Rest.empty())
        return std::nullopt;
    JsonValue::Array& list = ListOf(edited, entry->List).AsArray();
    if (entry->Index >= list.size())
        return std::nullopt;
    const auto at = list.begin() + static_cast<std::ptrdiff_t>(entry->Index);
    if (verb == "remove")
        list.erase(at);
    else if (verb == "up" && entry->Index > 0)
        std::iter_swap(at, at - 1);
    else if (verb == "down" && entry->Index + 1 < list.size())
        std::iter_swap(at, at + 1);
    else
        return std::nullopt;
    return edited;
}
