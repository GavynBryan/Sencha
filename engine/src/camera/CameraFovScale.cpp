#include <camera/CameraFovScale.h>

#include <ecs/World.h>

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
    if (Pulses.size() >= kCapacity)
    {
        const auto weakest = std::ranges::min_element(Pulses, {}, [&](const CameraFovPulse& running) {
            return std::abs(Partial(running.Scale, running.Timing.Level(running.Timing.Elapsed(now))) - 1.0f);
        });
        *weakest = pulse;
        return;
    }
    Pulses.push_back(pulse);
}

void CameraFovScaleState::Release(const LeaseOwner& holder, std::uint64_t tick, double tickSeconds)
{
    for (CameraFovPulse& pulse : Pulses)
        if (pulse.Timing.Holder.IsSet() && pulse.Timing.Holder == holder)
            pulse.Timing.ReleaseAfter(tick, tickSeconds);
}

void CameraFovScaleState::Update(const World& world, EntityId camera, const CameraEffectClock& now)
{
    Presented = 1.0f;
    PresentedCamera = camera;
    std::erase_if(Pulses, [&](CameraFovPulse& pulse) {
        if (pulse.Timing.Holder.IsSet() && !world.IsAlive(pulse.Timing.Holder.Entity))
            pulse.Timing.ReleaseAfter(now.Tick == 0 ? 0 : now.Tick - 1, now.TickSeconds);
        return pulse.Timing.Expired(pulse.Timing.Elapsed(now));
    });
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
