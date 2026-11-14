#pragma once

#include <ecs/ComponentAnnotations.h>

#include <type_traits>

//=============================================================================
// CameraBob
//
// On a camera: the view rises and dips with its subject's stride (StrideCycle),
// dropping at each footfall and swaying a little side to side over a stride. It
// moves the view, never turns it, so what is centred stays centred. Its strength
// follows the subject's ground speed up to FullSpeed, and nothing bobs off the
// ground or with no stride being counted.
//=============================================================================
struct SENCHA_COMPONENT("sencha.camera_bob")
       SENCHA_SCHEMA("CameraBob")
       SENCHA_SCENE_CHUNK("CBOB")
CameraBob
{
    SENCHA_FIELD("drop")
    SENCHA_LABEL("Footfall drop")
    SENCHA_TOOLTIP("Metres the view dips at each footfall, at full strength.")
    float Drop = 0.025f;

    SENCHA_FIELD("sway")
    SENCHA_LABEL("Sway")
    SENCHA_TOOLTIP("Metres the view sways to each side over a stride, at full strength.")
    float Sway = 0.012f;

    SENCHA_FIELD("full_speed")
    SENCHA_LABEL("Full strength at")
    SENCHA_TOOLTIP("Ground speed, in metres per second, at which the bob reaches full strength.")
    float FullSpeed = 6.0f;

    SENCHA_FIELD("response")
    SENCHA_LABEL("Response")
    SENCHA_TOOLTIP("Seconds the strength takes to follow a change of speed or footing.")
    float Response = 0.12f;
};

static_assert(std::is_trivially_copyable_v<CameraBob>, "CameraBob must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <camera/CameraBob.sencha.h>
#endif
