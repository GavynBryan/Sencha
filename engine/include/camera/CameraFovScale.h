#pragma once

#include <camera/CameraEffectTiming.h>

#include <cstddef>
#include <vector>

// Narrowest and widest a presented field of view may become, as a multiple of
// the camera's own: several pulses multiply, and the product stays inside.
inline constexpr float kCameraFovScaleMin = 0.25f;
inline constexpr float kCameraFovScaleMax = 4.0f;

struct CameraFovScaleSettings
{
    // How much of each pulse is presented: zero leaves every camera's field
    // of view exactly as authored, one is as the content asked.
    float Intensity = 1.0f;
};

struct CameraFovPulse
{
    CameraEffectTiming Timing;
    // The multiple of the camera's field of view at full strength.
    float Scale = 1.0f;
};

// A World resource, local to this machine and never replicated: field of view
// pulses being presented, and the multiple they come to for the active camera
// this frame. A multiple rather than an angle, so a camera's authored field of
// view stays its own.
class CameraFovScaleState
{
public:
    static constexpr std::size_t kCapacity = 8;

    CameraFovScaleSettings Settings;

    // When full, the pulse furthest from 1 at `now` is kept over the nearest.
    void Start(CameraFovPulse pulse, const CameraEffectClock& now);
    void Release(const LeaseOwner& holder, std::uint64_t tick, double tickSeconds);
    void Update(const World& world, EntityId camera, const CameraEffectClock& now);
    void Clear() { Pulses.clear(); Presented = 1.0f; PresentedCamera = {}; }

    [[nodiscard]] std::size_t Count() const { return Pulses.size(); }
    // This frame's multiple for `camera`; 1 for any other camera.
    [[nodiscard]] float ScaleFor(EntityId camera) const;

private:
    std::vector<CameraFovPulse> Pulses;
    float Presented = 1.0f;
    EntityId PresentedCamera;
};
