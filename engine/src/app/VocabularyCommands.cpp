#include <app/VocabularyCommands.h>

#include <app/Engine.h>
#include <authored/AuthoredArgumentMatch.h>
#include <authored/AuthoredConstantText.h>
#include <authored/AuthoredLiteral.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredValueText.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbBindingData.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <core/console/ConsoleService.h>
#include <core/identity/Id.h>
#include <ecs/EntityText.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <participant/LocalControl.h>
#include <participant/ParticipantLifecycle.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentEntityIndex.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
constexpr std::string_view kMe = "@me";
constexpr std::string_view kAssetScheme = "asset://";

struct Assignment
{
    std::string_view Key;
    std::string_view Value;
};

ConsoleResult Failure(ConsoleStatus status, std::string text)
{
    ConsoleResult result;
    result.Status = status;
    result.Error(std::move(text));
    return result;
}

std::string KeysOf(const DataFieldSchema& arguments)
{
    std::string keys;
    for (const DataFieldSchema& field : arguments.Children)
        keys += (keys.empty() ? "" : ", ") + field.Key;
    return keys.empty() ? std::string("none") : keys;
}

std::optional<std::vector<Assignment>> AssignmentsOf(std::string_view name, const DataFieldSchema& arguments,
                                                     std::span<const std::string> args, ConsoleResult& refusal)
{
    std::vector<Assignment> assignments;
    for (const std::string& arg : args)
    {
        const std::size_t equals = arg.find('=');
        if (equals == std::string::npos || equals == 0)
        {
            refusal = Failure(ConsoleStatus::InvalidArguments,
                              std::format("'{}' is not key=value; '{}' takes {}", arg, name, KeysOf(arguments)));
            return std::nullopt;
        }
        const Assignment assignment{ std::string_view(arg).substr(0, equals), std::string_view(arg).substr(equals + 1) };
        if (std::ranges::none_of(arguments.Children,
                                 [&](const DataFieldSchema& field) { return field.Key == assignment.Key; }))
        {
            refusal = Failure(ConsoleStatus::InvalidArguments,
                              std::format("'{}' takes no argument '{}'; it takes {}", name, assignment.Key,
                                          KeysOf(arguments)));
            return std::nullopt;
        }
        assignments.push_back(assignment);
    }
    return assignments;
}

EntityId LiveEntity(const World& world, std::string_view text)
{
    if (text == kMe)
        return LocalControlSubjectOf(world);
    const std::optional<EntityId> entity = ParseEntityText(text);
    return entity.has_value() && world.IsAlive(*entity) ? *entity : EntityId{};
}

std::string AssetPath(std::string_view text)
{
    return text.starts_with(kAssetScheme) ? std::string(text) : std::string(kAssetScheme) + std::string(text);
}

// An argument's text as a binding argument; a live entity becomes an input with its handle in `entity`.
bool BindingArgumentOf(const World& world, const DataFieldSchema& field, const Assignment& assignment,
                       VerbBindingArgument& out, std::optional<EntityId>& entity, std::string& error)
{
    const DataFieldKind kind = UnwrappedField(field).Kind;
    if (kind == DataFieldKind::Entity)
    {
        if (PersistentEntityIdFromString(assignment.Value).has_value())
        {
            out.Key = std::string(assignment.Key);
            out.Source = VerbArgumentSource::Entity;
            out.Text = std::string(assignment.Value);
            return true;
        }
        const EntityId live = LiveEntity(world, assignment.Value);
        if (!live.IsValid())
        {
            error = std::format("'{}' names no living entity: write @me, index:generation or a persistent id",
                                assignment.Value);
            return false;
        }
        out.Key = std::string(assignment.Key);
        out.Source = VerbArgumentSource::Input;
        out.Text = std::string(assignment.Key);
        entity = live;
        return true;
    }
    const bool asset = kind == DataFieldKind::AssetRef || kind == DataFieldKind::DataAssetRef;
    const std::optional<JsonValue> constant =
        AuthoredConstantFromText(field, asset ? std::string_view(AssetPath(assignment.Value)) : assignment.Value);
    if (!constant.has_value())
    {
        error = std::format("'{}' is not a value '{}' takes", assignment.Value, assignment.Key);
        return false;
    }
    std::vector<AssetRef> dependencies;
    return ParseVerbBindingArgument("console", std::string(assignment.Key), *constant, out, dependencies, error);
}

const GameplayTagRegistry* TagsOf(const World& world)
{
    return world.TryGetResource<GameplayTagRegistry>();
}
} // namespace

ConsoleResult RunVerbCommand(const VocabularyConsoleTarget& target, std::span<const std::string> args)
{
    if (args.empty())
        return Failure(ConsoleStatus::InvalidArguments, "verb <name> [key=value ...]");
    const VerbRegistry* verbs = FindVerbRegistry(std::as_const(target.Entities));
    const VerbId id = verbs != nullptr ? verbs->Find(args[0]) : VerbId{};
    const VerbDefinition* definition = verbs != nullptr ? verbs->Get(id) : nullptr;
    if (definition == nullptr || target.Verbs == nullptr)
        return Failure(ConsoleStatus::InvalidArguments, std::format("no verb '{}'", args[0]));
    if (definition->Kind == VerbKind::Lease)
        return Failure(ConsoleStatus::ValidationFailed,
                       std::format("'{}' is a lease, and a console line has no owner to hold it", args[0]));
    if (definition->Prediction == AuthoredPrediction::AuthorityOnly && !IsSimulationAuthority(target.Entities))
        return Failure(ConsoleStatus::ValidationFailed,
                       std::format("'{}' runs only on the authority; run it at the server's console", args[0]));

    ConsoleResult refusal;
    const std::optional<std::vector<Assignment>> assignments =
        AssignmentsOf(args[0], definition->Arguments, args.subspan(1), refusal);
    if (!assignments.has_value())
        return refusal;

    VerbBindingDesc desc;
    desc.Key = "console.verb";
    desc.KeyId = MakeVerbBindingKey(desc.Key);
    desc.VerbName = args[0];
    std::vector<AuthoredValue> inputs;
    for (const Assignment& assignment : *assignments)
    {
        const DataFieldSchema& field = *std::ranges::find(definition->Arguments.Children, assignment.Key,
                                                          &DataFieldSchema::Key);
        VerbBindingArgument argument;
        std::optional<EntityId> entity;
        std::string error;
        if (!BindingArgumentOf(target.Entities, field, assignment, argument, entity, error))
            return Failure(ConsoleStatus::InvalidArguments, std::move(error));
        if (entity.has_value())
        {
            desc.Inputs.push_back(argument.Text);
            inputs.push_back(AuthoredValue::Entity(*entity));
        }
        desc.Arguments.push_back(std::move(argument));
    }

    CompiledVerbBinding binding;
    std::vector<std::string> errors;
    if (!CompileVerbBinding(desc, MakeVerbBindingEnvironment(target.Entities), binding, errors))
    {
        ConsoleResult result;
        result.Status = ConsoleStatus::InvalidArguments;
        for (std::string& error : errors)
            result.Error(std::move(error));
        return result;
    }

    AuthoredValue answered;
    const VerbInvocationSource source{ .Instigator = LocalParticipantOf(target.Entities) };
    const VerbInvocationResult invoked = target.Verbs->Invoke(binding, inputs, source, &answered);
    const std::string described = DescribeVerbAnswer(*verbs, id, AnswerOf(invoked));
    ConsoleResult result;
    if (!invoked.Accepted())
    {
        result.Status = ConsoleStatus::ExecutionFailed;
        result.Error(std::format("{}: {}", args[0], described));
        return result;
    }
    if (definition->Result.has_value() && !answered.IsNone())
        result.Info(std::format("{}: {}; {} = {}", args[0], described, definition->Result->Key,
                                AuthoredValueText(answered, TagsOf(target.Entities))));
    else
        result.Info(std::format("{}: {}", args[0], described));
    return result;
}

ConsoleResult RunQueryCommand(const VocabularyConsoleTarget& target, std::span<const std::string> args)
{
    if (args.empty())
        return Failure(ConsoleStatus::InvalidArguments, "query <name> [key=value ...]");
    const AuthoredQueryRegistry* queries = FindAuthoredQueryRegistry(std::as_const(target.Entities));
    const AuthoredQueryHandle handle = queries != nullptr ? queries->Resolve(args[0]) : AuthoredQueryHandle{};
    const AuthoredQueryDefinition* definition = handle.Slot.IsValid() ? queries->Get(handle.Slot) : nullptr;
    if (definition == nullptr || target.Queries == nullptr)
        return Failure(ConsoleStatus::InvalidArguments, std::format("no query '{}'", args[0]));

    ConsoleResult refusal;
    const std::optional<std::vector<Assignment>> assignments =
        AssignmentsOf(args[0], definition->Arguments, args.subspan(1), refusal);
    if (!assignments.has_value())
        return refusal;

    std::vector<std::string_view> names;
    for (const Assignment& assignment : *assignments)
        names.push_back(assignment.Key);
    std::vector<std::string_view> undeclared;
    const std::vector<AuthoredArgumentSlot> slots = MatchAuthoredArguments(definition->Arguments, names, undeclared);
    const VerbBindingEnvironment environment = MakeVerbBindingEnvironment(target.Entities);
    const PersistentEntityIndex* index = target.Entities.TryGetResource<PersistentEntityIndex>();
    std::vector<AuthoredValue> arguments(slots.size());
    std::vector<std::string> errors;
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        const DataFieldSchema& field = *slots[i].Field;
        if (!slots[i].Given.has_value())
        {
            (void)CompileAuthoredDefault(field, "console", field.Key, arguments[i], errors);
            continue;
        }
        VerbBindingArgument argument;
        std::optional<EntityId> entity;
        std::string error;
        if (!BindingArgumentOf(target.Entities, field, (*assignments)[*slots[i].Given], argument, entity, error))
            return Failure(ConsoleStatus::InvalidArguments, std::move(error));
        if (entity.has_value())
            arguments[i] = AuthoredValue::Entity(*entity);
        else if (argument.Source == VerbArgumentSource::Entity)
        {
            const std::optional<PersistentEntityId> persistent = PersistentEntityIdFromString(argument.Text);
            const EntityId resolved = index != nullptr ? index->TryResolve(*persistent) : EntityId{};
            if (!resolved.IsValid())
                return Failure(ConsoleStatus::InvalidArguments,
                               std::format("'{}' is not in this World now", argument.Text));
            arguments[i] = AuthoredValue::Entity(resolved);
        }
        else
            (void)CompileAuthoredConstant(argument, field, environment, "console", arguments[i], errors);
    }
    if (!errors.empty())
    {
        ConsoleResult result;
        result.Status = ConsoleStatus::InvalidArguments;
        for (std::string& error : errors)
            result.Error(std::move(error));
        return result;
    }

    AuthoredValue answer;
    const AuthoredQueryStatus status = target.Queries->Evaluate(handle, arguments, answer);
    ConsoleResult result;
    if (status != AuthoredQueryStatus::Value)
    {
        result.Status = ConsoleStatus::ExecutionFailed;
        result.Error(std::format("{}: {}", args[0], AuthoredQueryStatusName(status)));
        return result;
    }
    result.Info(std::format("{}: {}", args[0], AuthoredValueText(answer, TagsOf(target.Entities))));
    return result;
}

void RegisterVocabularyCommands(ConsoleService& console, Engine& engine)
{
    const auto targetOf = [&engine] {
        return VocabularyConsoleTarget{ .Entities = engine.World().Entities(),
                                        .Verbs = engine.TryVerbs(),
                                        .Queries = engine.TryAuthoredQueries() };
    };
    console.Registry().RegisterCommand({
        .Name = "verb",
        .Owner = "engine",
        .Usage = "verb <name> [key=value ...]",
        .Help = "Invoke any declared verb, as the local participant. An entity is @me, index:generation or a "
                "persistent id; a lease cannot be invoked here.",
        .Examples = { "verb inventory.give into=@me definition=items/key.sdata count=2" },
        .Flags = ConsoleCommandFlags::Developer,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [targetOf](ConsoleExecutionContext&, std::span<const std::string> args) {
            return RunVerbCommand(targetOf(), args);
        },
    });
    console.Registry().RegisterCommand({
        .Name = "query",
        .Owner = "engine",
        .Usage = "query <name> [key=value ...]",
        .Help = "Ask any declared query and print its answer.",
        .Examples = { "query counter.value name=quest.keys" },
        .Flags = ConsoleCommandFlags::Developer,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [targetOf](ConsoleExecutionContext&, std::span<const std::string> args) {
            return RunQueryCommand(targetOf(), args);
        },
    });
}
