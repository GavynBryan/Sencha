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

    VerbAdmission AsVerbAdmission(SaveAdmission admission)
    {
        switch (admission)
        {
        case SaveAdmission::Accepted:
            return VerbAdmission::Accepted;
        case SaveAdmission::BadSlot:
            return VerbAdmission::InvalidArguments;
        default:
            return VerbAdmission::Refused;
        }
    }

    const char* Describe(SaveAdmission admission)
    {
        switch (admission)
        {
        case SaveAdmission::Accepted:
            return "accepted";
        case SaveAdmission::Busy:
            return "another is under way";
        case SaveAdmission::BadSlot:
            return "a slot is letters, digits, '-' and '_'";
        case SaveAdmission::NothingLoaded:
            return "nothing is loaded";
        case SaveAdmission::NotAuthority:
            return "this machine is not the authority";
        case SaveAdmission::InSession:
            return "a network session is running";
        }
        return "";
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
            result.Error(std::string(what) + " refused: " + Describe(admission));
        return result;
    }
}

void DeclareSavedGameVerbs(VerbRegistrationScope& scope)
{
    (void)scope.Declare(SaveVerb(kSaveWriteVerb, "Save", "Writes how the world differs from its authored content."));
    (void)scope.Declare(SaveVerb(kSaveRestoreVerb, "Restore",
                                 "Loads the saved level again and puts back what the save kept."));
}

VerbAdmission InvokeSaveWrite(SavedGames& saves, const VerbInvocation& invocation)
{
    return AsVerbAdmission(saves.RequestWrite(SlotOf(invocation)));
}

VerbAdmission InvokeSaveRestore(SavedGames& saves, const VerbInvocation& invocation)
{
    return AsVerbAdmission(saves.RequestRestore(SlotOf(invocation)));
}

void RegisterSavedGameCommands(ConsoleService& console, Engine& engine)
{
    const auto slotOf = [](std::span<const std::string> args) {
        return args.empty() ? std::string(kDefaultSaveSlot) : args[0];
    };
    console.Registry().RegisterCommand({
        .Name = std::string(kSaveWriteVerb),
        .Owner = "engine",
        .Usage = "save.write [slot]",
        .Help = "Write a save.",
        .Flags = ConsoleCommandFlags::AuthorityOnly,
        .RequiredPhase = ConsolePhase::GameLoaded,
        .Callback = [&engine, slotOf](ConsoleExecutionContext&, std::span<const std::string> args) {
            const std::string slot = slotOf(args);
            return Report("save", slot, engine.Saves().RequestWrite(slot));
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
