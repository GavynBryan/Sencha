#include <world/SimulationAuthority.h>

#include <ecs/World.h>

bool IsSimulationAuthority(const World& world)
{
    const SimulationAuthority* fact = world.TryGetResource<SimulationAuthority>();
    return fact == nullptr || fact->Authoritative;
}

std::uint64_t AuthorityTickOf(const World& world, std::uint64_t localTick)
{
    const SimulationAuthority* fact = world.TryGetResource<SimulationAuthority>();
    const std::int64_t offset = fact != nullptr ? fact->TickOffset : 0;
    if (offset >= 0)
        return localTick + static_cast<std::uint64_t>(offset);
    const auto back = static_cast<std::uint64_t>(-offset);
    return back > localTick ? 0 : localTick - back;
}
