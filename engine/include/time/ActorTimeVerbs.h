#pragma once

#include <authored/AuthoredAnnotations.h>
#include <authored/AuthoredApiDefinition.h>
#include <authored/VerbInvocation.h>
#include <ecs/EntityId.h>
#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>

class FixedSimulationLoop;
class World;

// Scales an actor's time for as long as the lease's owner holds it, from the tick
// after it is acquired through the tick it is released on.
class ActorTimeOperations
{
public:
    ActorTimeOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    SENCHA_LEASE("time.scale")
    SENCHA_LABEL("Scale time")
    SENCHA_CATEGORY("Time")
    SENCHA_DESCRIPTION("Runs an actor's time faster or slower while held: its movement, effects, phases and animation.")
    SENCHA_PREDICTION(Simulation)
    VerbAdmission Scale(const VerbInvocation& invocation,
                        SENCHA_TARGET("target", ActorTime) SENCHA_LABEL("Target")
                            SENCHA_DESCRIPTION("The actor whose time runs differently.") EntityId target,
                        SENCHA_ARG("scale") SENCHA_LABEL("Scale") SENCHA_DESCRIPTION("One is unchanged; a half is half speed.")
                            SENCHA_RANGE(0.01, kActorScaleHardMaximumQ16 / kActorTimeOne) float scale);
    SENCHA_LEASE_RELEASE("time.scale")
    VerbAdmission EndScale(const VerbInvocation& invocation);

    [[nodiscard]] const World& AuthoredWorld() const { return *Entities; }

private:
    World* Entities;
    const FixedSimulationLoop* Clock;
};

#if !defined(SENCHA_CODEGEN)
#  include <time/ActorTimeVerbs.sencha.h>
#endif
