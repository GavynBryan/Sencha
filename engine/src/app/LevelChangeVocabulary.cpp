#include <app/LevelChangeVocabulary.h>

#include <app/LevelChange.h>
#include <authored/AuthoredValue.h>
#include <authored/AuthoredValueTraits.h>
#include <ecs/World.h>
#include <participant/ParticipantLifecycle.h>
#include <physics/SensorOverlaps.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <optional>

#include <string>
#include <utility>
#include <vector>

namespace
{
    DataFieldSchema Choice(std::string key, std::string label, std::string tooltip,
                           std::vector<DataEnumChoice> choices)
    {
        DataFieldSchema field = MakeDataField(DataFieldKind::Enum, std::move(key), std::move(label), std::move(tooltip));
        field.EnumChoices = std::move(choices);
        return field;
    }

    EntityId RootOf(const World& world, EntityId entity)
    {
        for (int depth = 0; depth < 64 && world.IsRegistered<Parent>(); ++depth)
        {
            const Parent* parent = world.TryGet<Parent>(entity);
            if (parent == nullptr || !world.IsAlive(parent->Entity))
                break;
            entity = parent->Entity;
        }
        return entity;
    }

    std::vector<EntityId> OccupantsOf(const World& world, EntityId sensor)
    {
        std::vector<EntityId> occupants;
        const SensorOverlaps* overlaps = world.TryGetResource<SensorOverlaps>();
        if (overlaps == nullptr)
            return occupants;
        for (const SensorPair& pair : overlaps->Pairs())
        {
            if (pair.Sensor != sensor || !world.IsAlive(pair.Activator))
                continue;
            const EntityId root = RootOf(world, pair.Activator);
            if (root != sensor && !ParticipantWithBody(world, root).IsValid()
                && std::ranges::find(occupants, root) == occupants.end())
                occupants.push_back(root);
        }
        return occupants;
    }
}

void DeclareLevelChangeVerbs(VerbRegistrationScope& scope, AuthoredEventRegistrationScope& events)
{
    VerbDefinition change;
    change.Name = std::string(kLevelChangeVerb);
    change.DisplayName = "Change level";
    change.Description = "Replaces the loaded level with another, or the same one again, saying what it keeps.";
    change.Category = "Levels";
    change.Refusals = VerbRefusalsOf<LevelChangeAdmission>();
    DataFieldSchema kind = Choice("kind", "Kind", "A single scene, or a partitioned world.",
                                  { { "scene", "Scene", {} }, { "world", "World", {} } });
    kind.Required = false;
    kind.Default = std::string("scene");
    DataFieldSchema arrival = MakeDataField(DataFieldKind::String, "arrival", "Arrival",
                                            "The landmark bodies arrive at; the game's spawn when empty.");
    arrival.Required = false;
    arrival.Default = std::string();
    DataFieldSchema landmark = MakeDataField(DataFieldKind::String, "landmark", "Landmark",
                                             "A landmark both levels have: what crosses keeps its place from it.");
    landmark.Required = false;
    landmark.Default = std::string();
    change.Arguments.Children = {
        MakeDataField(DataFieldKind::String, "level", "Level", "The level's name, as `map` or `world` takes it."),
        std::move(kind),
        Choice("memory", "Memory", "Remembered enters it as the game left it; authored as content made it.",
               { { "remembered", "Remembered", {} }, { "authored", "Authored", {} } }),
        Choice("bodies", "Bodies", "Kept carries the same bodies across; fresh spawns the game's prefab.",
               { { "kept", "Kept", {} }, { "fresh", "Fresh", {} } }),
        std::move(arrival),
        std::move(landmark),
        AuthoredField<std::optional<EntityId>>("carry_within", "Carry within",
                                               "A volume whose occupants cross with the bodies, by the landmark."),
    };
    (void)scope.Declare(std::move(change));

    AuthoredEventDefinition arrived;
    arrived.Name = std::string(kLevelArrivedEvent);
    arrived.DisplayName = "Arrived from another level";
    arrived.Description = "Something a level change carried arrived at this landmark, once for each.";
    arrived.Category = "Levels";
    arrived.Payload.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "instigator", "Instigator", "What arrived."));
    (void)events.Declare(std::move(arrived));
}

VerbAnswer InvokeLevelChange(LevelChangeOperations& operations, const VerbInvocation& invocation)
{
    std::string_view name;
    std::string_view kind;
    std::string_view memory;
    std::string_view bodies;
    std::string_view arrival;
    if (invocation.Arguments == nullptr || !invocation.Arguments->TryGetString(0, name) || name.empty())
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0);
    if (!invocation.Arguments->TryGetEnum(1, kind))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 1);
    if (!invocation.Arguments->TryGetEnum(2, memory))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 2);
    if (!invocation.Arguments->TryGetEnum(3, bodies))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 3);
    (void)invocation.Arguments->TryGetString(4, arrival);
    std::string_view landmark;
    (void)invocation.Arguments->TryGetString(5, landmark);
    std::optional<EntityId> carryWithin;
    if (!ReadAuthoredArgument(*invocation.Arguments, 6, carryWithin))
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 6);
    const LevelReference level{ .Kind = kind == "world" ? LevelKind::World : LevelKind::Scene, .Name = std::string(name) };
    LevelChangeRequest request = LevelChangeRequest::Travel(
        level, memory == "authored" ? LevelMemory::Authored : LevelMemory::Remembered,
        bodies == "kept" ? LevelChangeBodies::Kept : LevelChangeBodies::Fresh);
    request.At(std::string(arrival));
    if (carryWithin.has_value())
        request.Carrying(OccupantsOf(operations.Entities, *carryWithin), std::string(landmark));
    return VerbAnswerOf(operations.Changes.Request(std::move(request)));
}
