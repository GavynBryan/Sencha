#include <physics/HitRegionResolver.h>

#include <gameplay_tags/GameplayTagRegistry.h>
#include <physics/PhysicsQueries.h>

std::optional<HitRegionHit> HitRegionResolver::Resolve(const World& world, EntityId target, const Vec3d& origin,
                                                       const Vec3d& unitDirection, float maxDistance)
{
    if (!world.IsRegistered<HitRegion>() || !world.IsRegistered<WorldTransform>() || !target.IsValid())
        return std::nullopt;
    if (QueriedWorld != &world)
    {
        Regions.emplace(world);
        QueriedWorld = &world;
    }
    const bool parented = world.IsRegistered<Parent>();
    std::optional<HitRegionHit> best;
    std::int32_t bestPriority = 0;
    InlineString<64> bestName;
    Regions->ForEachChunk([&](auto& view) {
        const auto regions = view.template Read<HitRegion>();
        const auto poses = view.template Read<WorldTransform>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
        {
            const EntityId part = view.Entity(row);
            const Parent* parent = parented ? world.TryGet<Parent>(part) : nullptr;
            if (part != target && (parent == nullptr || parent->Entity != target))
                continue;
            const std::optional<float> distance =
                RaycastShape(regions[row].Shape, poses[row].Value, origin, unitDirection, maxDistance);
            if (!distance)
                continue;
            const std::int32_t priority = regions[row].Priority;
            const bool wins = !best || *distance < best->Distance
                || (*distance == best->Distance
                    && (priority > bestPriority || (priority == bestPriority && part < best->Part)));
            if (!wins)
                continue;
            best = HitRegionHit{ .Part = part, .Region = {}, .Distance = *distance };
            bestPriority = priority;
            bestName = regions[row].Region;
        }
    });
    if (best)
        if (const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
            best->Region = tags->FindTag(bestName.View());
    return best;
}
