#include <save/SavedGameVocabulary.h>

#include <app/Engine.h>
#include <app/VocabularyCommands.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbRegistry.h>
#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleService.h>
#include <core/identity/Id.h>
#include <ecs/EntityText.h>
#include <ecs/World.h>
#include <participant/LocalControl.h>
#include <save/EngineSaveSections.h>
#include <save/SavedGames.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentEntityIndex.h>

#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    VerbDefinition SaveVerb(std::string_view name, std::string displayName, std::string description)
    {
        VerbDefinition verb;
        verb.Name = std::string(name);
        verb.DisplayName = std::move(displayName);
        verb.Description = std::move(description);
        verb.Category = "Saves";
        verb.Refusals = VerbRefusalsOf<SaveAdmission>();
        DataFieldSchema slot = MakeDataField(DataFieldKind::String, "slot", "Slot",
                                             "Letters, digits, '-' and '_'; the default slot when empty.");
        slot.Required = false;
        slot.Default = std::string();
        verb.Arguments.Children.push_back(std::move(slot));
        return verb;
    }

    std::string SlotOf(const VerbInvocation& invocation)
    {
        std::string_view slot;
        if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetString(0, slot) || slot.empty())
            return std::string(kDefaultSaveSlot);
        return std::string(slot);
    }

    VerbAnswer SaveAnswer(SaveAdmission admission)
    {
        return admission == SaveAdmission::BadSlot ? VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0)
                                                   : VerbAnswerOf(admission);
    }

    EntityId EntityNamed(const World& world, std::string_view text)
    {
        if (text == "me")
            return LocalControlSubjectOf(world);
        if (const std::optional<EntityId> entity = ParseEntityText(text))
            return world.IsAlive(*entity) ? *entity : EntityId{};
        const std::optional<PersistentEntityId> id = PersistentEntityIdFromString(text);
        const PersistentEntityIndex* index = world.TryGetResource<PersistentEntityIndex>();
        return id.has_value() && index != nullptr ? index->TryResolve(*id) : EntityId{};
    }

    ConsoleResult RunAlias(Engine& engine, std::string_view verb, std::span<const std::string> args)
    {
        std::vector<std::string> line{ std::string(verb) };
        for (std::size_t i = 0; i < args.size(); ++i)
            line.push_back(std::format("{}={}", i == 0 ? "slot" : "arrival", args[i]));
        return RunVerbCommand(VocabularyConsoleTarget{ .Entities = engine.World().Entities(),
                                                       .Verbs = engine.TryVerbs(),
                                                       .Queries = engine.TryAuthoredQueries() },
                              line);
    }
}

void DeclareSavedGameVerbs(VerbRegistrationScope& scope)
{
    VerbDefinition write = SaveVerb(kSaveWriteVerb, "Save", "Writes how the world differs from its authored content.");
    DataFieldSchema arrival = MakeDataField(DataFieldKind::String, "arrival", "Arrival",
                                            "Where the saved body comes back; written with the save.");
    arrival.Required = false;
    arrival.Default = std::string();
    write.Arguments.Children.push_back(std::move(arrival));
    (void)scope.Declare(std::move(write));
    (void)scope.Declare(SaveVerb(kSaveRestoreVerb, "Restore",
                                 "Loads the saved level again and puts back what the save kept."));
}

VerbAnswer InvokeSaveWrite(SavedGames& saves, const VerbInvocation& invocation)
{
    std::string_view arrival;
    if (invocation.Arguments != nullptr)
        (void)invocation.Arguments->TryGetString(1, arrival);
    return SaveAnswer(saves.RequestWrite(SlotOf(invocation), std::string(arrival)));
}

VerbAnswer InvokeSaveRestore(SavedGames& saves, const VerbInvocation& invocation)
{
    return SaveAnswer(saves.RequestRestore(SlotOf(invocation)));
}

void RegisterSavedGameCommands(ConsoleService& console, Engine& engine)
{
    console.Registry().RegisterCommand({
        .Name = std::string(kSaveWriteVerb),
        .Owner = "engine",
        .Usage = "save.write [slot] [arrival]",
        .Help = "verb save.write slot=<slot> arrival=<arrival>: write a save, naming where its body comes back.",
        .Flags = ConsoleCommandFlags::AuthorityOnly,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&, std::span<const std::string> args) {
            return RunAlias(engine, kSaveWriteVerb, args);
        },
    });
    console.Registry().RegisterCommand({
        .Name = std::string(kSaveRestoreVerb),
        .Owner = "engine",
        .Usage = "save.restore [slot]",
        .Help = "verb save.restore slot=<slot>: restore a save; its level loads again with what it kept.",
        .Flags = ConsoleCommandFlags::AuthorityOnly,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&, std::span<const std::string> args) {
            return RunAlias(engine, kSaveRestoreVerb, args);
        },
    });
    console.Registry().RegisterCommand({
        .Name = "save.describe",
        .Owner = "engine",
        .Usage = "save.describe <me | index:generation | persistent id>",
        .Help = "List what a save holds of an entity: each component it carries and its persistence.",
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&, std::span<const std::string> args) {
            ConsoleResult result;
            const World& world = engine.World().Entities();
            const EntityId entity = args.size() == 1 ? EntityNamed(world, args[0]) : EntityId{};
            if (!world.IsAlive(entity))
            {
                result.Status = ConsoleStatus::InvalidArguments;
                result.Error("name a living entity: me, index:generation or a persistent id");
                return result;
            }
            for (std::string& line : DescribeSavedEntity(world, entity, engine.SceneSerializers()))
                result.Info(std::move(line));
            return result;
        },
    });
}
