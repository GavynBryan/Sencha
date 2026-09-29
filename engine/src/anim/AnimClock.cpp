#include <anim/AnimClock.h>

#include <ecs/World.h>
#include <world/SimulationAuthority.h>
#include <world/SimulationTimeline.h>

AnimClock AnimClockAt(const World& world, std::uint64_t localTick)
{
    AnimClock clock = AnimClock::Uniform(AuthorityTickOf(world, localTick));
    if (const PredictedSimulation* predicted = world.TryGetResource<PredictedSimulation>();
        predicted != nullptr && predicted->Entity.IsValid())
    {
        clock.Predicted = predicted->Entity;
        clock.PredictedNow = SimulationTickOf(world, predicted->Entity, localTick);
    }
    return clock;
}
