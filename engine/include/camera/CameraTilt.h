#pragma once

#include <ecs/ComponentAnnotations.h>
#include <math/Angle.h>

#include <type_traits>

//=============================================================================
// CameraTilt
//
// On a camera: the view rolls into its subject's sideways motion, as far as
// MaxRoll at FullSpeed. Read from where the subject is drawn, so anything that
// moves it sideways tilts the view: running, an air strafe, a push, a vehicle.
//=============================================================================
struct SENCHA_COMPONENT("sencha.camera_tilt")
       SENCHA_SCHEMA("CameraTilt")
       SENCHA_SCENE_CHUNK("CTLT")
CameraTilt
{
    SENCHA_FIELD("max_roll")
    SENCHA_DEGREES
    SENCHA_LABEL("Most roll")
    SENCHA_TOOLTIP("How far the view rolls at full sideways speed. Stored in radians.")
    float MaxRoll = ToRadians(1.5f);

    SENCHA_FIELD("full_speed")
    SENCHA_LABEL("Most roll at")
    SENCHA_TOOLTIP("Sideways speed, in metres per second, at which the roll is greatest.")
    float FullSpeed = 6.0f;

    SENCHA_FIELD("response")
    SENCHA_LABEL("Response")
    SENCHA_TOOLTIP("Seconds the roll takes to follow a change of sideways speed.")
    float Response = 0.1f;
};

static_assert(std::is_trivially_copyable_v<CameraTilt>, "CameraTilt must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <camera/CameraTilt.sencha.h>
#endif
