#include <camera/CameraEffectTiming.h>

#include <camera/CameraSubject.h>
#include <ecs/World.h>

#include <algorithm>
#include <cmath>

namespace
{
double Smooth(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

bool Duration(float seconds)
{
    return std::isfinite(seconds) && seconds >= 0.0f;
}
} // namespace

bool IsValidCameraEnvelope(const CameraEnvelope& envelope)
{
    return Duration(envelope.Attack) && Duration(envelope.Hold) && Duration(envelope.Release);
}

CameraEffectTiming CameraEffectTiming::Once(const CameraEnvelope& envelope, std::uint64_t tick, EntityId subject)
{
    return { .Envelope = envelope,
             .StartTick = tick,
             .ReleaseAt = static_cast<double>(envelope.Attack) + envelope.Hold,
             .Subject = subject,
             .Holder = {} };
}

CameraEffectTiming CameraEffectTiming::Held(const CameraEnvelope& envelope, std::uint64_t tick, EntityId subject,
                                            LeaseOwner holder)
{
    return { .Envelope = envelope, .StartTick = tick, .ReleaseAt = kHeld, .Subject = subject, .Holder = holder };
}

double CameraEffectTiming::Elapsed(const CameraEffectClock& now) const
{
    // Started by the presentation of StartTick, so it is zero once that tick has run.
    const double ticks = static_cast<double>(now.Tick) - static_cast<double>(StartTick) - 1.0 + now.Alpha;
    return std::max(0.0, ticks * now.TickSeconds);
}

void CameraEffectTiming::ReleaseAfter(std::uint64_t tick, double tickSeconds)
{
    const double at = std::max(0.0, (static_cast<double>(tick) - static_cast<double>(StartTick)) * tickSeconds);
    ReleaseAt = std::min(ReleaseAt, at);
    Holder = {};
}

float CameraEffectTiming::Level(double elapsed) const
{
    const auto rise = [&](double t) { return Envelope.Attack > 0.0f ? Smooth(t / Envelope.Attack) : 1.0; };
    if (elapsed < ReleaseAt)
        return static_cast<float>(rise(elapsed));
    if (Envelope.Release <= 0.0f)
        return 0.0f;
    return static_cast<float>(rise(ReleaseAt) * (1.0 - Smooth((elapsed - ReleaseAt) / Envelope.Release)));
}

bool CameraEffectTiming::Expired(double elapsed) const
{
    return elapsed >= ReleaseAt + Envelope.Release;
}

EntityId CameraViewSubject(const World& world, EntityId camera)
{
    const CameraSubject* subject = world.IsRegistered<CameraSubject>() ? world.TryGet<CameraSubject>(camera) : nullptr;
    return subject != nullptr && subject->Subject.IsValid() ? subject->Subject : camera;
}

bool CameraViewIsOwnedBy(const World& world, EntityId camera, EntityId subject)
{
    return !subject.IsValid() || subject == camera || subject == CameraViewSubject(world, camera);
}
