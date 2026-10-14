#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class FixedSimulationLoop;
class World;

// Scale an actor's time for as long as the lease's owner holds it, from the tick
// after it is acquired through the tick it is released on.
inline constexpr std::string_view kTimeScaleLease = "time.scale";

void DeclareActorTimeVerbs(VerbRegistrationScope& scope);

class ActorTimeOperations
{
public:
    ActorTimeOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    VerbAdmission Scale(const VerbInvocation& invocation);

private:
    World* Entities;
    const FixedSimulationLoop* Clock;
};

inline VerbAdmission InvokeTimeScale(ActorTimeOperations& operations, const VerbInvocation& invocation)
{
    return operations.Scale(invocation);
}
