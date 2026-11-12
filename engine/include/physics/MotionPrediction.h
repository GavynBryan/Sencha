#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

class PhysicsQueries;

struct MotionPredictionShape
{
    float BodyRadius = 0.45f;
    // How far below each step ground must be for the walk to go on.
    float GroundBelow = 2.5f;
    int Steps = 4;
};

// Where something moving like this will be `seconds` from now if it keeps going:
// along the level, stopping short of a wall and at the last step with ground
// under it. A straight line would lead a shot through a wall the subject is
// about to stop at, or off a ledge it will not walk off.
[[nodiscard]] Vec3d PredictMotion(const PhysicsQueries& physics, const Vec3d& from, const Vec3d& velocity,
                                  float seconds, EntityId subject, const MotionPredictionShape& shape = {});
