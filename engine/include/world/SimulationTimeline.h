#pragma once

#include <ecs/EntityId.h>

#include <cstdint>

// The entity this process simulates ahead of the authority, named on the command
// timeline its commands are stamped in (docs/gameplay/animation.md, "Timing across
// machines"). The host publishes it beside SimulationAuthority; absent, nothing is predicted.
struct PredictedSimulation
{
    EntityId Entity;
    std::int64_t CommandTickOffset = 0;
};

class World;

[[nodiscard]] bool IsLocallyPredicted(const World& world, EntityId entity);

// `localTick` as `entity`'s simulation names it: the command timeline for the
// entity this process predicts, the authority's estimated present for any other.
// Floors at zero.
[[nodiscard]] std::uint64_t SimulationTickOf(const World& world, EntityId entity, std::uint64_t localTick);
