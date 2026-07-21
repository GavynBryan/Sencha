#pragma once

#include <math/Vec.h>

struct PhysicsContext;
class PhysicsStepSystem;

// Drives CharacterController entities through the simulation-wide mover pool
// owned by PhysicsStepSystem. The physics-domain partition set selects which
// movers reconcile and advance; dormant partitions retain component state but
// have no CharacterVirtual backend object.
class CharacterControllerSystem
{
public:
    explicit CharacterControllerSystem(PhysicsStepSystem& step);

    void Physics(PhysicsContext& ctx);

private:
    PhysicsStepSystem* Step;
    Vec3d Gravity = Vec3d(0.0f, -9.81f, 0.0f);
};
