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
#include <save/SaveSlotFiles.h>
#include <save/SavedGames.h>
#include <world/RuntimeWorld.h>
#include <world/identity/PersistentEntityIndex.h>

#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    std::string SlotOrDefault(std::string_view slot)
    {
        return slot.empty() ? std::string(kDefaultSaveSlot) : std::string(slot);
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

VerbRefusal<SaveAdmission> SavedGameOperations::Write(std::string_view slot, std::string_view arrival)
{
    return SaveAnswer(Saves->RequestWrite(SlotOrDefault(slot), std::string(arrival)));
}

VerbRefusal<SaveAdmission> SavedGameOperations::Restore(std::string_view slot)
{
    return SaveAnswer(Saves->RequestRestore(SlotOrDefault(slot)));
}

VerbRefusal<SaveAdmission> SavedGameOperations::Delete(std::string_view slot)
{
    if (!SaveSlotFiles::IsSlotName(slot))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0);
    return Saves->RequestDelete(std::string(slot));
}

VerbRefusal<SaveAdmission> SavedGameOperations::Copy(std::string_view from, std::string_view to)
{
    if (!SaveSlotFiles::IsSlotName(from))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0);
    if (!SaveSlotFiles::IsSlotName(to))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 1);
    return Saves->RequestCopy(std::string(from), std::string(to));
}

void RegisterSavedGameCommands(ConsoleService& console, Engine& engine)
{
    console.Registry().RegisterCommand({
        .Name = std::string(AuthoredApiDefinition<SavedGameOperations>::Name_Write),
        .Owner = "engine",
        .Usage = "save.write [slot] [arrival]",
        .Help = "verb save.write slot=<slot> arrival=<arrival>: write a save, naming where its body comes back.",
        .Flags = ConsoleCommandFlags::AuthorityOnly,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&, std::span<const std::string> args) {
            return RunAlias(engine, AuthoredApiDefinition<SavedGameOperations>::Name_Write, args);
        },
    });
    console.Registry().RegisterCommand({
        .Name = std::string(AuthoredApiDefinition<SavedGameOperations>::Name_Restore),
        .Owner = "engine",
        .Usage = "save.restore [slot]",
        .Help = "verb save.restore slot=<slot>: restore a save; its level loads again with what it kept.",
        .Flags = ConsoleCommandFlags::AuthorityOnly,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine](ConsoleExecutionContext&, std::span<const std::string> args) {
            return RunAlias(engine, AuthoredApiDefinition<SavedGameOperations>::Name_Restore, args);
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
