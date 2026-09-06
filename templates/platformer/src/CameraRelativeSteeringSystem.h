#pragma once

#include <controller/LookOrientation.h>
#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementIntent.h>
#include <world/transform/TransformComponents.h>

#include <optional>

#include <math/Vec.h>

struct FixedLogicContext;

// The steering rule of a third-person game: pushing the stick up runs away from
// the camera, whichever way the body happens to be facing, and the body turns
// to face where it runs. Both halves are pure so they can be tested on their
// own; the system applies them to every controlled body each tick.

// The planar wish direction for a stick reading, in the frame of a camera at
// `cameraYaw`. Unit length or shorter; zero for no input.
[[nodiscard]] Vec3d CameraRelativeWish(float cameraYaw, Vec2d move);

// The yaw a body should face to run along `wish`, or `current` when there is
// nothing to face.
[[nodiscard]] float FacingYawFor(const Vec3d& wish, float current);

struct CameraRelativeSteeringSystem
{
    void FixedLogic(FixedLogicContext& ctx);

private:
    std::optional<Query<Write<MovementIntent>, Write<LocalTransform>, Read<GameplayTagContainer>>> Steer;
};
