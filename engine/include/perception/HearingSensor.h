#pragma once

#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>

// An ear on the entity. A stimulus is heard when the ear is within the
// stimulus's radius times this sensitivity; a stimulus meant for this entity
// alone is heard wherever it is.
struct SENCHA_COMPONENT("HearingSensor")
       SENCHA_SCHEMA("HearingSensor")
       SENCHA_SCENE_CHUNK("HEAR")
HearingSensor
{
    SENCHA_FIELD("sensitivity")
    SENCHA_OPTIONAL
    SENCHA_TOOLTIP("How far this entity hears, as a share of each sound's own reach.")
    float Sensitivity = 1.0f;

    SENCHA_FIELD("ear_offset")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Ear offset")
    SENCHA_TOOLTIP("Where the ear sits, relative to the entity.")
    Vec3d EarOffset = Vec3d(0.0f, 1.6f, 0.0f);
};

#if !defined(SENCHA_CODEGEN)
#  include <perception/HearingSensor.sencha.h>
#endif
