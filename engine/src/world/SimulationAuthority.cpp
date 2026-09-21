#include <world/SimulationAuthority.h>

#include <ecs/World.h>

bool IsSimulationAuthority(const World& world)
{
    const SimulationAuthority* fact = world.TryGetResource<SimulationAuthority>();
    return fact == nullptr || fact->Authoritative;
}
