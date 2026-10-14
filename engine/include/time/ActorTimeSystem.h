#pragma once

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>

#include <cstdint>
#include <optional>

class ConsoleRegistry;
class StoragePartitionSet;
class World;

// First thing in a fixed tick: each actor's scale is set from the leases in force
// on the tick, and its clock advances by it, so everything after reads the tick's
// actor time. A lease whose holder's entity is gone ends here.
class ActorTimeSystem
{
public:
    // Registers time.actor_scale_min and time.actor_scale_max; null keeps the defaults.
    explicit ActorTimeSystem(ConsoleRegistry* console = nullptr);

    void FixedLogic(FixedLogicContext& ctx);

    [[nodiscard]] ActorScaleLimits& Limits() { return Bounds; }

private:
    ActorScaleLimits Bounds;
    std::optional<Query<Write<ActorTimeLeases>, Write<ActorTime>>> Scaled;
    std::optional<Query<Write<ActorTime>>> Clocks;
};

// One tick's advance outside a schedule, for tests and tools.
void AdvanceActorTime(World& world, std::uint64_t tick, const ActorScaleLimits& limits = {});

// Whole actor ticks since `mark` (an earlier ActorTicks reading); simulation ticks
// for an entity with no clock.
[[nodiscard]] std::uint64_t ActorTicksSince(const World& world, EntityId actor, std::uint64_t mark,
                                            std::uint64_t simulationTick);
