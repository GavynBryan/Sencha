#pragma once

#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagId.h>
#include <physics/components/HitRegion.h>
#include <world/transform/TransformComponents.h>

#include <optional>

struct HitRegionHit
{
    EntityId Part;
    GameplayTagId Region;
    float Distance = 0.0f;
};

// Which of an entity's hit regions a strike landed on. Owner thread.
class HitRegionResolver
{
public:
    // The region on `target`, or on an entity whose parent it is, that the ray
    // enters first; ties go to the higher priority, then the lower entity.
    // Nullopt when the ray crosses none within `maxDistance`.
    [[nodiscard]] std::optional<HitRegionHit> Resolve(const World& world, EntityId target, const Vec3d& origin,
                                                      const Vec3d& unitDirection, float maxDistance);

private:
    const World* QueriedWorld = nullptr;
    std::optional<Query<Read<HitRegion>, Read<WorldTransform>>> Regions;
};
