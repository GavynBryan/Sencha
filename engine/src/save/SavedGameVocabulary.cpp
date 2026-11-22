#include <save/SavedGameVocabulary.h>

#include <app/Engine.h>
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

#include <span>
#include <string>
#include <utility>

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

    VerbAnswer AnswerOf(SaveAdmission admission)
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

    ConsoleResult Report(std::string_view what, const std::string& slot, SaveAdmission admission)
    {
        ConsoleResult result;
        if (admission == SaveAdmission::Accepted)
            result.Info(std::string(what) + " slot '" + slot + "'");
        else
            result.Error(std::string(what) + " refused: " + std::string(FindEnumValue(admission)->Tooltip));
        return result;
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
    return AnswerOf(saves.RequestWrite(SlotOf(invocation), std::string(arrival)));
}

VerbAnswer InvokeSaveRestore(SavedGames& saves, const VerbInvocation& invocation)
{
    return AnswerOf(saves.RequestRestore(SlotOf(invocation)));
}

void RegisterSavedGameCommands(ConsoleService& console, Engine& engine)
{
    const auto slotOf = [](std::span<const std::string> args) {
        return args.empty() ? std::string(kDefaultSaveSlot) : args[0];
    };
    console.Registry().RegisterCommand({
        .Name = std::string(kSaveWriteVerb),
        .Owner = "engine",
        .Usage = "save.write [slot] [arrival]",
        .Help = "Write a save, naming where its body comes back.",
        .Flags = ConsoleCommandFlags::AuthorityOnly,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine, slotOf](ConsoleExecutionContext&, std::span<const std::string> args) {
            const std::string slot = slotOf(args);
            return Report("save", slot, engine.Saves().RequestWrite(slot, args.size() > 1 ? args[1] : std::string()));
        },
    });
    console.Registry().RegisterCommand({
        .Name = std::string(kSaveRestoreVerb),
        .Owner = "engine",
        .Usage = "save.restore [slot]",
        .Help = "Restore a save: its level loads again with what it kept.",
        .Flags = ConsoleCommandFlags::AuthorityOnly,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine, slotOf](ConsoleExecutionContext&, std::span<const std::string> args) {
            const std::string slot = slotOf(args);
            return Report("restore", slot, engine.Saves().RequestRestore(slot));
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
