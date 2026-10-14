#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <time/FrameClock.h>

#include <string_view>

class FixedSimulationLoop;
class World;

// Apply an effect to an entity with attributes, unowned: an instant effect lands, a
// lasting one runs out on its own.
inline constexpr std::string_view kEffectApplyVerb = "effect.apply";
// Hold a lasting effect on an entity for as long as the lease's owner holds it.
inline constexpr std::string_view kEffectHoldLease = "effect.hold";

// The contracts, declared with the engine's own vocabulary.
void DeclareEffectVerbs(VerbRegistrationScope& scope);

// The authored API's door to effects. Both refuse a target without an attribute set,
// or without an attribute the effect modifies.
class EffectOperations
{
public:
    EffectOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    VerbAdmission Apply(const VerbInvocation& invocation);
    VerbAdmission Hold(const VerbInvocation& invocation);

private:
    [[nodiscard]] FixedSimTime Now(const VerbInvocation& invocation) const;

    World* Entities;
    const FixedSimulationLoop* Clock;
};

inline VerbAdmission InvokeEffectApply(EffectOperations& operations, const VerbInvocation& invocation)
{
    return operations.Apply(invocation);
}

inline VerbAdmission InvokeEffectHold(EffectOperations& operations, const VerbInvocation& invocation)
{
    return operations.Hold(invocation);
}
