#include <world/LevelLandmark.h>

#include <ecs/World.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/TransformComponents.h>

namespace
{
    constexpr int kDeepestLandmarkParent = 64;

    [[nodiscard]] Transform3f WorldPlaceOf(const World& world, EntityId entity)
    {
        Transform3f place = Transform3f::Identity();
        for (int depth = 0; entity.IsValid() && depth < kDeepestLandmarkParent; ++depth)
        {
            if (const LocalTransform* local = world.TryGet<LocalTransform>(entity))
                place = local->Value * place;
            const Parent* parent = world.TryGet<Parent>(entity);
            entity = parent != nullptr && world.IsAlive(parent->Entity) ? parent->Entity : EntityId{};
        }
        return place;
    }

    [[nodiscard]] std::uint64_t OrderOf(const World& world, EntityId entity)
    {
        const PersistentIdComponent* id = world.TryGet<PersistentIdComponent>(entity);
        return id != nullptr ? id->Id.Value : ~std::uint64_t{ 0 };
    }
}

std::optional<LevelLandmarkPlace> FindLevelLandmark(const World& world, std::string_view name)
{
    if (name.empty() || !world.IsRegistered<LevelLandmark>())
        return std::nullopt;
    EntityId found;
    world.ForEachComponent<LevelLandmark>([&](EntityId entity, const LevelLandmark& landmark) {
        if (landmark.Name.View() == name && (!found.IsValid() || OrderOf(world, entity) < OrderOf(world, found)))
            found = entity;
    });
    if (!found.IsValid())
        return std::nullopt;
    const Transform3f place = WorldPlaceOf(world, found);
    LevelLandmarkPlace landmark{ .Entity = found };
    landmark.Place.Position = place.Position;
    landmark.Place.Rotation = place.Rotation;
    return landmark;
}
