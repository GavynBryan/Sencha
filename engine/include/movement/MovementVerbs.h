#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <ecs/Query.h>
#include <movement/components/HeldMotion.h>

#include <optional>
#include <string_view>

class World;

inline constexpr std::string_view kMovementBurstLease = "movement.burst";
inline constexpr std::string_view kMovementTetherLease = "movement.tether";
inline constexpr std::string_view kMovementModeLease = "movement.mode";
inline constexpr std::string_view kMovementLaunchVerb = "movement.launch";
inline constexpr std::string_view kMovementImpulseVerb = "movement.impulse";

void DeclareMovementVerbs(VerbRegistrationScope& scope);

// The movement operations' provider. Each lease holds one of a character's motion
// slots (movement/components/HeldMotion.h) for its owner, and lets it go; a timed
// burst completes when its time is up, and a tether when its anchor goes. A launch
// is instant: it writes the forced motion channels for the coming composition.
class MovementOperations
{
public:
    explicit MovementOperations(World& world)
        : Entities(&world)
    {
    }

    VerbAdmission Burst(const VerbInvocation& invocation);
    VerbAdmission Tether(const VerbInvocation& invocation);
    VerbAdmission Mode(const VerbInvocation& invocation);
    VerbAdmission Launch(const VerbInvocation& invocation);
    VerbAdmission Impulse(const VerbInvocation& invocation);

private:
    // Whether `owner` still holds a slot of `T`, looked for on its own entity first.
    template <typename T>
    [[nodiscard]] bool Holds(std::optional<Query<Read<T>>>& everyone, const LeaseOwner& owner);

    World* Entities;
    std::optional<Query<Read<MotionBursts>>> Bursts;
    std::optional<Query<Read<TetherSession>>> Tethers;
};

inline VerbAdmission InvokeMovementBurst(MovementOperations& operations, const VerbInvocation& invocation)
{
    return operations.Burst(invocation);
}

inline VerbAdmission InvokeMovementTether(MovementOperations& operations, const VerbInvocation& invocation)
{
    return operations.Tether(invocation);
}

inline VerbAdmission InvokeMovementMode(MovementOperations& operations, const VerbInvocation& invocation)
{
    return operations.Mode(invocation);
}

inline VerbAdmission InvokeMovementLaunch(MovementOperations& operations, const VerbInvocation& invocation)
{
    return operations.Launch(invocation);
}

inline VerbAdmission InvokeMovementImpulse(MovementOperations& operations, const VerbInvocation& invocation)
{
    return operations.Impulse(invocation);
}
