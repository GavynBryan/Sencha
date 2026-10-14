#pragma once

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <time/ActorTime.h>

#include <optional>

class World;

// Advances every active actor's clock by its scale, first thing in a fixed tick,
// so everything after reads the tick's actor time.
class ActorTimeSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);

private:
    std::optional<Query<Write<ActorTime>>> Clocks;
};

void AdvanceActorTime(World& world);
