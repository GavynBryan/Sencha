#pragma once

#include <ecs/ComponentAnnotations.h>
#include <physics/CollisionShapeSchema.h>

//=============================================================================
// Collider
//
// Data-only ECS component. Gives an entity a collision volume. Its body's
// motion comes from a sibling RigidBody (static world geometry if absent); its
// collision layer is derived from that motion plus IsTrigger, so it is not
// authored here. The cooked-mesh shape used for static brush collision is added
// with the cook path (a CollisionShape asset handle alongside the primitive);
// that handle is runtime state and never persists.
//=============================================================================
struct SENCHA_COMPONENT("sencha.physics.collider")
       SENCHA_SCHEMA("collider")
       SENCHA_SCENE_CHUNK("COLL")
Collider
{
    // A valid Mesh (a cooked shape resolved into the CollisionShapeCache, the
    // static-brush-collision case) takes precedence over the inline primitive
    // Shape used by authored/runtime colliders.
    SENCHA_FIELD("shape")
    CollisionShape Shape{};
    CollisionShapeHandle Mesh{};
    SENCHA_FIELD("is_trigger")
    SENCHA_LABEL("Sensor")
    SENCHA_TOOLTIP("Reports what overlaps it instead of blocking.")
    bool IsTrigger = false;
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/components/Collider.sencha.h>
#endif
