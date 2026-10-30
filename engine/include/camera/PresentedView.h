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
// Read it from the frame's PresentedFrame (camera/PresentedFrame.h), which is
// published after FrameUpdate and before UI layout. The effects are summed when
// that is published, so calling PresentCameraView earlier in the frame -- in
// FrameUpdate, say -- gets the view the previous frame was drawn with.
struct PresentedView
{
    Transform3f Pose;
    float FovYRadians = 0.0f;
};

// `placed` is the camera's pose before its effects, as PresentationPoseOf gives
// it: blended to presentation time where it or its parent carries history.
[[nodiscard]] PresentedView PresentCameraView(const World& world,
                                              EntityId camera,
                                              const CameraComponent& component,
                                              const Transform3f& placed);
