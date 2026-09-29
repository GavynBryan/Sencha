#pragma once

#include <anim/AnimTypes.h>
#include <ecs/EntityId.h>

#include <cstdint>

class World;

// The tick each entity is simulating on one fixed step: the entity this process
// predicts on its command timeline, every other one on the authority's estimated
// present. See world/SimulationTimeline.h.
struct AnimClock
{
    AnimTick Now = 0;
    AnimTick PredictedNow = 0;
    EntityId Predicted;

    [[nodiscard]] AnimTick For(EntityId entity) const
    {
        return Predicted.IsValid() && entity == Predicted ? PredictedNow : Now;
    }

    // Every entity on one timeline: tests, the preview and a process predicting nothing.
    [[nodiscard]] static AnimClock Uniform(AnimTick now) { return AnimClock{ now, now, {} }; }
};

[[nodiscard]] AnimClock AnimClockAt(const World& world, std::uint64_t localTick);
