#pragma once

#include <ecs/ComponentAnnotations.h>

//=============================================================================
// CharacterController
//
// Authored physical shape of a kinematic capsule character. Configuration
// only: the per-tick motion comes from MotionRequest and the achieved facts go
// back out as SupportState and KinematicState, so this component is read by
// the mover pool and never used as an in/out mailbox.
//
// Every field defaults to its member initializer, which is what lets content
// state only the dimensions it cares about -- a prefab that says nothing but
// "radius" still gets a whole capsule.
//=============================================================================
struct SENCHA_COMPONENT("sencha.physics.character_controller")
       SENCHA_SCHEMA("CharacterController")
       SENCHA_SCENE_CHUNK("CHCT")
CharacterController
{
    SENCHA_FIELD("radius")
    float Radius = 0.3f;

    SENCHA_FIELD("height")
    float Height = 1.8f;

    SENCHA_FIELD("slope_limit_degrees")
    SENCHA_LABEL("Slope limit")
    SENCHA_TOOLTIP("Steepest ground the body will walk up, in degrees.")
    float SlopeLimitDegrees = 50.0f;

    SENCHA_FIELD("step_height")
    SENCHA_TOOLTIP("Tallest ledge the body steps over instead of stopping at.")
    float StepHeight = 0.35f;

    SENCHA_FIELD("ground_snap_distance")
    SENCHA_LABEL("Ground snap")
    SENCHA_TOOLTIP("How far below the feet ground still counts as ground, "
                   "which is what keeps a body on a downward slope instead "
                   "of launching off it.")
    float GroundSnapDistance = 0.25f;

    SENCHA_FIELD("skin_width")
    SENCHA_TOOLTIP("Gap kept between the capsule and what it touches.")
    float SkinWidth = 0.02f;
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/components/CharacterController.sencha.h>
#endif
