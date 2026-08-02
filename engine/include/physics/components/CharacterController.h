#pragma once

#include <ecs/ComponentTypeId.h>

//=============================================================================
// CharacterController
//
// Authored physical shape of a kinematic capsule character. Configuration
// only: the per-tick motion comes from MotionRequest and the achieved facts go
// back out as SupportState and KinematicState, so this component is read by
// the mover pool and never used as an in/out mailbox.
//=============================================================================
struct CharacterController
{
    float Radius = 0.3f;
    float Height = 1.8f;
    float SlopeLimitDegrees = 50.0f;
    float StepHeight = 0.35f;
    float GroundSnapDistance = 0.25f;
    float SkinWidth = 0.02f;
};

SENCHA_DECLARE_COMPONENT_TYPE(CharacterController, "sencha.physics.character_controller");
