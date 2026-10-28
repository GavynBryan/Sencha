#include <camera/CameraShake.h>

#include <core/hash/SplitMix64.h>
#include <ecs/World.h>
#include <math/Angle.h>
#include <math/ResponseCurve.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr std::size_t kRotationChannel = 0;
constexpr std::size_t kTranslationChannel = 3;

// A lattice gradient in [-1, 1], fixed by the seed, the channel and the lattice point.
double Gradient(std::uint64_t seed, std::size_t channel, std::int64_t lattice)
{
    std::uint64_t state = seed ^ (static_cast<std::uint64_t>(channel) << 56) ^ static_cast<std::uint64_t>(lattice);
    (void)SplitMix64(state);
    return static_cast<double>(SplitMix64(state) >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

// One-dimensional gradient noise: smooth, zero at every lattice point, within [-1, 1].
double Noise(std::uint64_t seed, std::size_t channel, double x)
{
    const double floor = std::floor(x);
    const double f = x - floor;
    const auto lattice = static_cast<std::int64_t>(floor);
    const double u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    const double near = Gradient(seed, channel, lattice) * f;
    const double far = Gradient(seed, channel, lattice + 1) * (f - 1.0);
    return std::clamp(2.0 * (near + (far - near) * u), -1.0, 1.0);
}

Vec3d Channels(const CameraShakeChannels& kick, const CameraShakeChannels& noise, bool rotation, std::uint64_t seed,
               double x)
{
    const Vec3d& fixed = rotation ? kick.Rotation : kick.Translation;
    const Vec3d& amplitude = rotation ? noise.Rotation : noise.Translation;
    const std::size_t first = rotation ? kRotationChannel : kTranslationChannel;
    Vec3d out = fixed;
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (amplitude[axis] != 0.0f)
            out[axis] += amplitude[axis] * static_cast<float>(Noise(seed, first + axis, x));
    return out;
}

Vec3d Clamp(Vec3d value, float limit)
{
    for (std::size_t axis = 0; axis < 3; ++axis)
        value[axis] = std::clamp(value[axis], -limit, limit);
    return value;
}

float FalloffAt(const CameraShakeInstance& shake, Vec3d cameraPosition)
{
    if (!shake.Point || !shake.Profile.Falloff)
        return 1.0f;
    const ResponseCurve curve{ .Shape = ResponseCurveShape::Linear,
                               .Direction = ResponseCurveDirection::Falling,
                               .Low = shake.Profile.Falloff->Inner,
                               .High = shake.Profile.Falloff->Outer };
    return EvaluateResponseCurve(curve, (cameraPosition - *shake.Point).Magnitude());
}
} // namespace

CameraShakeOffset EvaluateCameraShake(const CameraShakeProfile& profile, std::uint64_t seed, double elapsed,
                                      float level)
{
    if (level <= 0.0f)
        return {};
    const double x = elapsed * profile.Frequency;
    return { .Rotation = Channels(profile.Kick, profile.Noise, true, seed, x) * level,
             .Translation = Channels(profile.Kick, profile.Noise, false, seed, x) * level };
}

Transform3f ApplyCameraShake(const Transform3f& pose, const CameraShakeOffset& offset)
{
    if (offset.IsIdentity())
        return pose;
    const Quatf turn = Quatf::FromPitchYawRoll(ToRadians(offset.Rotation.X), ToRadians(offset.Rotation.Y),
                                               ToRadians(offset.Rotation.Z));
    Transform3f shaken = pose;
    shaken.Position = pose.Position + pose.Rotation.RotateVector(offset.Translation);
    shaken.Rotation = (pose.Rotation * turn).Normalized();
    return shaken;
}

void CameraShakeState::Start(CameraShakeInstance shake, const CameraEffectClock& now)
{
    if (Shakes.size() >= kCapacity)
    {
        const auto weakest = std::ranges::min_element(Shakes, {}, [&](const CameraShakeInstance& running) {
            return running.Timing.Level(running.Timing.Elapsed(now)) * running.Scale;
        });
        *weakest = std::move(shake);
        return;
    }
    Shakes.push_back(std::move(shake));
}

void CameraShakeState::Release(const LeaseOwner& holder, std::uint64_t tick, double tickSeconds)
{
    for (CameraShakeInstance& shake : Shakes)
        if (shake.Timing.Holder.IsSet() && shake.Timing.Holder == holder)
            shake.Timing.ReleaseAfter(tick, tickSeconds);
}

void CameraShakeState::Update(const World& world, EntityId camera, Vec3d cameraPosition, const CameraEffectClock& now)
{
    Presented = {};
    PresentedCamera = camera;
    std::erase_if(Shakes, [&](CameraShakeInstance& shake) {
        if (shake.Timing.Holder.IsSet() && !world.IsAlive(shake.Timing.Holder.Entity))
            shake.Timing.ReleaseAfter(now.Tick == 0 ? 0 : now.Tick - 1, now.TickSeconds);
        return shake.Timing.Expired(shake.Timing.Elapsed(now));
    });
    if (Settings.Scale <= 0.0f || !camera.IsValid())
        return;
    for (const CameraShakeInstance& shake : Shakes)
    {
        if (!CameraViewIsOwnedBy(world, camera, shake.Timing.Subject))
            continue;
        const double elapsed = shake.Timing.Elapsed(now);
        const float level = shake.Timing.Level(elapsed) * shake.Scale * FalloffAt(shake, cameraPosition);
        const CameraShakeOffset one = EvaluateCameraShake(shake.Profile, shake.Seed, elapsed, level);
        Presented.Rotation += one.Rotation;
        Presented.Translation += one.Translation;
    }
    Presented.Rotation = Clamp(Presented.Rotation * Settings.Scale, Settings.MaxAngle);
    Presented.Translation = Clamp(Presented.Translation * Settings.Scale, Settings.MaxOffset);
}

CameraShakeOffset CameraShakeState::OffsetFor(EntityId camera) const
{
    return camera.IsValid() && camera == PresentedCamera ? Presented : CameraShakeOffset{};
}
