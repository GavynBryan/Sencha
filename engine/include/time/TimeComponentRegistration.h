#pragma once

#include <time/ActorTime.h>
#include <world/ComponentRegistrar.h>
#include <world/ComponentSet.h>

// Each actor's own clock. Run-time state the ability kit and movement read; it is
// neither saved nor, yet, sent.
using TimeComponents = ComponentSet<ActorTime>;

inline void RegisterTimeComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<TimeComponents>();
}
