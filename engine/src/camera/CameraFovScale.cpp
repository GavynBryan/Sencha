#include <camera/CameraFovScale.h>

#include <ecs/World.h>
#include <presentation/HeldEffects.h>

#include <algorithm>
#include <cmath>

namespace
{
// A pulse at envelope `level`: its scale reached in proportion, so the field of
// view eases from the camera's own and back.
float Partial(float scale, float level)
{
    return 1.0f + (scale - 1.0f) * level;
}
} // namespace

void CameraFovScaleState::Start(CameraFovPulse pulse, const CameraEffectClock& now)
{
    StartHeldEffect(Pulses, kCapacity, pulse, [&](const CameraFovPulse& running) {
        return std::abs(Partial(running.Scale, running.Timing.Level(running.Timing.Elapsed(now))) - 1.0f);
    });
}

void CameraFovScaleState::Release(const LeaseOwner& holder, std::uint64_t tick, double tickSeconds)
{
    ReleaseHeldEffects(Pulses, holder, tick, tickSeconds);
}

void CameraFovScaleState::Update(const World& world, EntityId camera, const CameraEffectClock& now)
{
    Presented = 1.0f;
    PresentedCamera = camera;
    RetireHeldEffects(Pulses, world, now);
    if (Settings.Intensity <= 0.0f || !camera.IsValid())
        return;
    for (const CameraFovPulse& pulse : Pulses)
        if (CameraViewIsOwnedBy(world, camera, pulse.Timing.Subject))
            Presented *= Partial(pulse.Scale, pulse.Timing.Level(pulse.Timing.Elapsed(now)) * Settings.Intensity);
    Presented = std::clamp(Presented, kCameraFovScaleMin, kCameraFovScaleMax);
}

float CameraFovScaleState::ScaleFor(EntityId camera) const
{
    return camera.IsValid() && camera == PresentedCamera ? Presented : 1.0f;
}
