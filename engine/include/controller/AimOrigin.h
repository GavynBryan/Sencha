#pragma once

#include <ecs/ComponentAnnotations.h>
#include <math/Vec.h>

#include <type_traits>

// Where a body aims from: an offset from its origin, turned with its facing -- the
// eyes of a character, the muzzle of a turret. A property of the thing, authored
// with it; a body without one aims from its origin.
struct SENCHA_COMPONENT("sencha.aim_origin")
       SENCHA_SCHEMA("AimOrigin")
       SENCHA_SCENE_CHUNK("AIMO")
AimOrigin
{
    SENCHA_FIELD("offset")
    SENCHA_LABEL("Offset")
    SENCHA_TOOLTIP("Where this body aims from, relative to its origin and turned with its facing.")
    Vec3d Offset = Vec3d::Zero();
};

static_assert(std::is_trivially_copyable_v<AimOrigin>,
              "AimOrigin must be trivially copyable to live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <controller/AimOrigin.sencha.h>
#endif
