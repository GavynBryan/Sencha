#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class FixedSimulationLoop;
class World;

// Ask `target` to present `intent`: held until anim.cancel, for a fixed number of
// ticks, or for the tick it is asked on.
inline constexpr std::string_view kAnimRequestVerb = "anim.request";
// End the held request this producer asked `target` for, by its intent.
inline constexpr std::string_view kAnimCancelVerb = "anim.cancel";

// The contracts, declared with the engine's own vocabulary.
void DeclareAnimRequestVerbs(VerbRegistrationScope& scope);

// The authored API's door to animation requests, for producers with no ability to
// own one: level logic, scripted gameplay, AI, props. Both go through
// RequestAnimation and CancelAnimation, the one path every producer uses.
class AnimRequestOperations
{
public:
    AnimRequestOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    VerbAdmission Request(const VerbInvocation& invocation);
    VerbAdmission Cancel(const VerbInvocation& invocation);

private:
    World* Entities;
    const FixedSimulationLoop* Clock;
};

inline VerbAdmission InvokeAnimRequest(AnimRequestOperations& operations, const VerbInvocation& invocation)
{
    return operations.Request(invocation);
}

inline VerbAdmission InvokeAnimCancel(AnimRequestOperations& operations, const VerbInvocation& invocation)
{
    return operations.Cancel(invocation);
}
