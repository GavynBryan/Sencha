#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>

// Where an application came from: who applied it, and where and which way the
// strike that carried it went, when it had one. Reactions read it; applying an
// effect never does.
struct EffectSource
{
    EntityId Instigator{};
    Vec3d Point = Vec3d::Zero();
    Vec3d Direction = Vec3d::Zero();
    bool HasPoint = false;
    bool HasDirection = false;
};
