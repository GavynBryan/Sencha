#include <world/LevelLandmark.h>

#include <ecs/World.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/transform/DerivedTransform.h>

namespace
{
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
    const Transform3f place = ComposeWorldTransform(world, found);
    LevelLandmarkPlace landmark{ .Entity = found };
    landmark.Place.Position = place.Position;
    landmark.Place.Rotation = place.Rotation;
    return landmark;
}
