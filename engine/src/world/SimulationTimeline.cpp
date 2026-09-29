#include <world/SimulationTimeline.h>

#include <ecs/World.h>
#include <world/SimulationAuthority.h>

namespace
{
    std::uint64_t Offset(std::uint64_t tick, std::int64_t offset)
    {
        if (offset >= 0)
            return tick + static_cast<std::uint64_t>(offset);
        const auto back = static_cast<std::uint64_t>(-offset);
        return back > tick ? 0 : tick - back;
    }
}

bool IsLocallyPredicted(const World& world, EntityId entity)
{
    const PredictedSimulation* predicted = world.TryGetResource<PredictedSimulation>();
    return predicted != nullptr && entity.IsValid() && predicted->Entity == entity;
}

std::uint64_t SimulationTickOf(const World& world, EntityId entity, std::uint64_t localTick)
{
    if (IsLocallyPredicted(world, entity))
        return Offset(localTick, world.TryGetResource<PredictedSimulation>()->CommandTickOffset);
    return AuthorityTickOf(world, localTick);
}
