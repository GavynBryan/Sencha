#pragma once

#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>
#include <world/ComponentRegistrar.h>
#include <world/ComponentSet.h>

// Each actor's own clock and the leases that scale it. Run-time state the ability
// kit, effects and movement read; neither saved nor, yet, sent.
using TimeComponents = ComponentSet<ActorTime, ActorTimeLeases, ActorTimeDilated>;

inline void RegisterTimeComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<TimeComponents>();
}
