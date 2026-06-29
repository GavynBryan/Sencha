#pragma once

#include <ecs/ComponentTypeId.h>
#include <math/Vec.h>

//=============================================================================
// CharacterController
//
// Data-only ECS component for a kinematic capsule character. Capsule dimensions
// and slope limit are authored; DesiredVelocity is the planar move intent that
// gameplay (fixed logic) writes each tick; Grounded is written back by the
// CharacterControllerSystem. The persistent vertical velocity lives in the
// system's CharacterMover, not here.
//=============================================================================
struct CharacterController
{
    float Radius = 0.3f;
    float Height = 1.8f;
    float SlopeLimitDegrees = 50.0f;
    Vec3d DesiredVelocity = Vec3d::Zero();
    bool Grounded = false;
};

SENCHA_DECLARE_COMPONENT_TYPE(CharacterController, "sencha.physics.character_controller");
