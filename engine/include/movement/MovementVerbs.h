#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>
#include <ecs/Query.h>
#include <movement/components/HeldMotion.h>

#include <optional>
#include <string_view>

class World;

// Drive a character at a velocity while held, or for a time: a lunge, a dash, a launch.
inline constexpr std::string_view kMovementBurstLease = "movement.burst";
// Tie a character to an anchor while held, pulled toward it beyond the rope's length.
inline constexpr std::string_view kMovementTetherLease = "movement.tether";
// Put a character in a locomotion mode while held; letting go returns it to free.
inline constexpr std::string_view kMovementModeLease = "movement.mode";
inline constexpr std::string_view kMovementApplyVelocityVerb = "movement.apply_velocity";

void DeclareMovementVerbs(VerbRegistrationScope& scope);

// The movement leases' provider: each holds one of a character's motion slots
// (movement/components/HeldMotion.h) for its owner, and lets it go. A timed burst
// completes when its time is up, and a tether when its anchor goes.
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
    VerbAdmission ApplyVelocity(const VerbInvocation& invocation);

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

inline VerbAdmission InvokeMovementApplyVelocity(MovementOperations& operations, const VerbInvocation& invocation)
{
    return operations.ApplyVelocity(invocation);
}
