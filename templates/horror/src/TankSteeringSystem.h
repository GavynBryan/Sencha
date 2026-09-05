#pragma once

#include <math/Vec.h>

struct FixedLogicContext;

// Tank controls: the stick's vertical axis walks the body along its own
// facing, forwards or back, and the horizontal axis turns it in place. The
// camera has nothing to do with where the body goes, which is what lets a fixed
// camera cut between angles without the controls flipping under the player.
struct TankStep
{
    Vec3d Wish;   // planar, unit length or shorter
    float Yaw;    // the body's facing after this tick
};

// `move.Y` walks, `move.X` turns at `turnRate` radians per second.
[[nodiscard]] TankStep TankSteer(float yaw, Vec2d move, float turnRate, float dt);

struct TankSteeringSystem
{
    // How fast A/D turn the body.
    float TurnRate = 2.4f;

    void FixedLogic(FixedLogicContext& ctx);
};
