#pragma once

#include <camera/CameraEffectTiming.h>
#include <camera/CameraShakeProfile.h>
#include <camera/CameraViewOffset.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// One profile's offset `elapsed` seconds in, at envelope `level`. A pure
// function of its arguments: the noise is a function of time and seed.
[[nodiscard]] CameraViewOffset EvaluateCameraShake(const CameraShakeProfile& profile, std::uint64_t seed,
                                                   double elapsed, float level);

struct CameraShakeSettings
{
    // Multiplies every shake; zero presents none at all.
    float Scale = 1.0f;
    // Bounds on what all shakes together may do: the largest rotation about any
    // one axis, in degrees, and the largest offset along any one, in metres.
    float MaxAngle = 6.0f;
    float MaxOffset = 0.25f;
};

struct CameraShakeInstance
{
    CameraEffectTiming Timing;
    CameraShakeProfile Profile;
    float Scale = 1.0f;
    std::uint64_t Seed = 0;
    std::optional<Vec3d> Point;
};

// A World resource, local to this machine and never replicated: the shakes
// being presented, and what they add up to for the active camera this frame.
class CameraShakeState
{
public:
    static constexpr std::size_t kCapacity = 16;

    CameraShakeSettings Settings;

    // When full, the shake contributing least at `now` makes way.
    void Start(CameraShakeInstance shake, const CameraEffectClock& now);
    void Release(const LeaseOwner& holder, std::uint64_t tick, double tickSeconds);
    // Retires what has run out or lost its holder, and sums what `camera`
    // presents, a camera at `cameraPosition`.
    void Update(const World& world, EntityId camera, Vec3d cameraPosition, const CameraEffectClock& now);
    void Clear() { Shakes.clear(); Presented = {}; PresentedCamera = {}; }

    [[nodiscard]] std::size_t Count() const { return Shakes.size(); }
    // This frame's offset for `camera`; identity for any other camera.
    [[nodiscard]] CameraViewOffset OffsetFor(EntityId camera) const;

private:
    std::vector<CameraShakeInstance> Shakes;
    CameraViewOffset Presented;
    EntityId PresentedCamera;
};
