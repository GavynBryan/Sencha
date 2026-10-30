#pragma once

#include <ecs/EntityId.h>
#include <math/geometry/3d/Transform3d.h>

class World;
struct CameraComponent;

// The view a camera is drawn with this frame: its pose moved by its presented
// shake, and its vertical field of view scaled by its presented pulses and held
// within what the renderer projects. Render extraction draws from it; anything
// laid over the frame -- a reticle, a marker -- reads the same view, so it agrees
// with the picture rather than with the camera that aiming and traces read.
//
// Read it from PresentedFrame; the effects are summed when that is published.
struct PresentedView
{
    Transform3f Pose;
    float FovYRadians = 0.0f;
};

[[nodiscard]] PresentedView PresentCameraView(const World& world,
                                              EntityId camera,
                                              const CameraComponent& component,
                                              const Transform3f& placed);
