#include <app/LevelChangeVocabulary.h>

#include <app/LevelChange.h>
#include <authored/AuthoredValue.h>
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

void DeclareLevelChangeEvents(AuthoredEventRegistrationScope& events)
{
    AuthoredEventDefinition arrived;
    arrived.Name = std::string(kLevelArrivedEvent);
    arrived.DisplayName = "Arrived from another level";
    arrived.Description = "Something a level change carried arrived at this landmark, once for each.";
    arrived.Category = "Levels";
    arrived.Payload.Children.push_back(
        MakeDataField(DataFieldKind::Entity, "instigator", "Instigator", "What arrived."));
    (void)events.Declare(std::move(arrived));
}

VerbRefusal<LevelChangeAdmission> LevelChangeOperations::Change(std::string_view level, LevelMemory memory,
                                                                LevelChangeBodies bodies, LevelKind kind,
                                                                std::string_view arrival, std::string_view landmark,
                                                                std::optional<EntityId> carryWithin)
{
    if (level.empty())
        return VerbArgumentAnswer(VerbAdmission::InvalidArguments, 0);
    LevelChangeRequest request =
        LevelChangeRequest::Travel(LevelReference{ .Kind = kind, .Name = std::string(level) }, memory, bodies);
    request.At(std::string(arrival));
    if (carryWithin.has_value())
        request.Carrying(OccupantsOf(*Entities, *carryWithin), std::string(landmark));
    return Changes->Request(std::move(request));
}
