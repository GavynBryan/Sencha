#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class FixedSimulationLoop;
class VerbDispatcher;
class World;

// Shake the view once, by a camera.shake profile.
inline constexpr std::string_view kCameraShakeVerb = "camera.shake";
// Shake the view by a profile for as long as it is held, then release.
inline constexpr std::string_view kCameraShakeHoldLease = "camera.shake.hold";

void DeclareCameraShakeVerbs(VerbRegistrationScope& scope);

// The shake operations' provider, over the World's CameraShakeState. Cosmetic:
// a presentation runs them on each machine that presents, and nothing in the
// simulation reads what they do. Profiles are the data assets `verbs` checks
// arguments against.
class CameraShakeOperations
{
public:
    CameraShakeOperations(World& world, const FixedSimulationLoop& clock, const VerbDispatcher& verbs)
        : Entities(&world)
        , Clock(&clock)
        , Verbs(&verbs)
    {
    }

    VerbAdmission Shake(const VerbInvocation& invocation);
    VerbAdmission Hold(const VerbInvocation& invocation);

private:
    VerbAdmission Start(const VerbInvocation& invocation, bool held);

    World* Entities;
    const FixedSimulationLoop* Clock;
    const VerbDispatcher* Verbs;
};

inline VerbAdmission InvokeCameraShake(CameraShakeOperations& operations, const VerbInvocation& invocation)
{
    return operations.Shake(invocation);
}

inline VerbAdmission InvokeCameraShakeHold(CameraShakeOperations& operations, const VerbInvocation& invocation)
{
    return operations.Hold(invocation);
}
