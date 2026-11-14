#pragma once

#include <camera/CameraEffectTiming.h>
#include <camera/CameraViewOffset.h>
#include <ecs/EntityId.h>

class World;
struct CameraBob;

struct CameraBobSettings
{
    // Multiplies every bob; zero presents none at all.
    float Scale = 1.0f;
};

// The view offset of `bob` at `cycle` through a stride, at `strength` from 0 to 1:
// at its lowest at each footfall (0 and one half), at its highest between them,
// and to one side and back over a stride.
[[nodiscard]] CameraViewOffset EvaluateCameraBob(const CameraBob& bob, float cycle, float strength);

// A World resource, local to this machine and never replicated: the bob the
// active camera presents this frame, and the strength it is easing toward.
class CameraBobState
{
public:
    CameraBobSettings Settings;

    // Reads the camera's CameraBob and its subject's StrideCycle at `now`.
    void Update(const World& world, EntityId camera, const CameraEffectClock& now);
    void Clear()
    {
        Presented = {};
        PresentedCamera = {};
        Strength = 0.0f;
        Step.Reset();
    }

    // This frame's offset for `camera`; identity for any other camera.
    [[nodiscard]] CameraViewOffset OffsetFor(EntityId camera) const;
    [[nodiscard]] float CurrentStrength() const { return Strength; }

private:
    CameraViewOffset Presented;
    EntityId PresentedCamera;
    float Strength = 0.0f;
    CameraEffectStep Step;
};
