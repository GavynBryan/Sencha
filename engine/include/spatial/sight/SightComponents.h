#pragma once

#include <ecs/ComponentAnnotations.h>
#include <math/Angle.h>
#include <math/Vec.h>

// An eye on the entity: where it sits, how far it reaches, and how wide it
// looks. It faces along the entity's world forward axis.
struct SENCHA_COMPONENT("SightSensor")
       SENCHA_SCHEMA("SightSensor")
       SENCHA_SCENE_CHUNK("SGHT")
SightSensor
{
    SENCHA_FIELD("range")
    SENCHA_TOOLTIP("Farthest distance, in metres, this entity can see.")
    float Range = 25.0f;

    SENCHA_FIELD("half_angle")
    SENCHA_DEGREES
    SENCHA_LABEL("Half angle")
    SENCHA_TOOLTIP("Angle from straight ahead to the edge of the view. 180 sees all around.")
    float HalfAngle = ToRadians(60.0f);

    SENCHA_FIELD("eye_offset")
    SENCHA_LABEL("Eye offset")
    SENCHA_TOOLTIP("Where the eye sits, relative to the entity.")
    Vec3d EyeOffset = Vec3d(0.0f, 1.6f, 0.0f);
};

// Marks an entity as something sight looks for. Says nothing about whether it
// matters to whoever sees it.
struct SENCHA_COMPONENT("SightTarget")
       SENCHA_SCHEMA("SightTarget")
       SENCHA_SCENE_CHUNK("SGTG")
SightTarget
{
    SENCHA_FIELD("sample_offset")
    SENCHA_LABEL("Sample offset")
    SENCHA_TOOLTIP("The point on this entity an eye has to reach, relative to the entity.")
    Vec3d SampleOffset = Vec3d(0.0f, 1.0f, 0.0f);
};

#if !defined(SENCHA_CODEGEN)
#  include <spatial/sight/SightComponents.sencha.h>
#endif
