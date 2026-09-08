#pragma once

#include <ecs/EntityId.h>

class Logger;
struct FrameUpdateContext;

// Where the orbit is looking from. Written by the look action each frame, read
// by the steering system for its frame: the two agree because they read one
// resource rather than each deriving a yaw of their own.
struct OrbitCameraState
{
    float Yaw = 0.0f;
    float Pitch = -0.45f;
    float Distance = 6.0f;
    float MinPitch = -1.2f;
    float MaxPitch = 0.2f;
    // Above the body's origin, so the boom pivots around its chest and not its feet.
    float PivotHeight = 1.0f;
};

//=============================================================================
// OrbitCameraSystem
//
// This game's camera policy: a camera of the game's own making orbits the body
// this machine drives, turned by the look action, at a fixed boom length. The
// body does not aim -- it faces where it runs -- so nothing here reads a
// LookOrientation; the look action turns the camera and only the camera.
//=============================================================================
struct OrbitCameraSystem
{
    Logger* Log = nullptr;

    void FrameUpdate(FrameUpdateContext& ctx);

private:
    EntityId Body;
    EntityId Camera;
};
