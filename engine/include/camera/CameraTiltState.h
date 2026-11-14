#pragma once

#include <camera/CameraEffectTiming.h>
#include <camera/CameraViewOffset.h>
#include <ecs/EntityId.h>
#include <math/Vec.h>

class World;
struct CameraTilt;

struct CameraTiltSettings
{
    // Multiplies every tilt; zero presents none at all.
    float Scale = 1.0f;
};

// Faster than this between two frames is a teleport, not motion to tilt into.
inline constexpr float kCameraTiltMaxSpeed = 100.0f;

// The roll, in degrees, of a view moving `sideways` metres per second to its right
// (negative to its left): into the motion, up to the tilt's MaxRoll.
[[nodiscard]] float CameraTiltRoll(const CameraTilt& tilt, float sideways);

// A World resource, local to this machine and never replicated: the roll the
// active camera presents this frame, eased toward its subject's sideways motion.
class CameraTiltState
{
public:
    CameraTiltSettings Settings;

    // Reads the camera's CameraTilt, and how far its subject is drawn from where it
    // was drawn last frame, measured along the camera's right.
    void Update(const World& world, EntityId camera, const CameraEffectClock& now);
    void Clear()
    {
        Presented = {};
        PresentedCamera = {};
        Roll = 0.0f;
        Step.Reset();
    }

    // This frame's offset for `camera`; identity for any other camera.
    [[nodiscard]] CameraViewOffset OffsetFor(EntityId camera) const;

private:
    CameraViewOffset Presented;
    EntityId PresentedCamera;
    float Roll = 0.0f;
    Vec3d LastSubjectPosition = Vec3d::Zero();
    CameraEffectStep Step;
};
