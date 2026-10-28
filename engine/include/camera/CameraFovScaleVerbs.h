#pragma once

#include <authored/VerbInvocation.h>
#include <authored/VerbRegistry.h>

#include <string_view>

class FixedSimulationLoop;
class World;

// Scale the view's field of view for a moment.
inline constexpr std::string_view kCameraFovScaleVerb = "camera.fov_scale";
// Scale it for as long as it is held, then ease back.
inline constexpr std::string_view kCameraFovScaleHoldLease = "camera.fov_scale.hold";

void DeclareCameraFovScaleVerbs(VerbRegistrationScope& scope);

// The field of view operations' provider, over the World's CameraFovScaleState.
// Cosmetic, like the shake operations.
class CameraFovScaleOperations
{
public:
    CameraFovScaleOperations(World& world, const FixedSimulationLoop& clock)
        : Entities(&world)
        , Clock(&clock)
    {
    }

    VerbAdmission Pulse(const VerbInvocation& invocation);
    VerbAdmission Hold(const VerbInvocation& invocation);

private:
    VerbAdmission Start(const VerbInvocation& invocation, bool held);

    World* Entities;
    const FixedSimulationLoop* Clock;
};

inline VerbAdmission InvokeCameraFovScale(CameraFovScaleOperations& operations, const VerbInvocation& invocation)
{
    return operations.Pulse(invocation);
}

inline VerbAdmission InvokeCameraFovScaleHold(CameraFovScaleOperations& operations, const VerbInvocation& invocation)
{
    return operations.Hold(invocation);
}
