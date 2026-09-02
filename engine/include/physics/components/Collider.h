#pragma once

#include <ecs/ComponentAnnotations.h>
#include <physics/CollisionShape.h>

//=============================================================================
// Collider
//
// Data-only ECS component. Gives an entity a collision volume. Its body's
// motion comes from a sibling RigidBody (static world geometry if absent); its
// collision layer is derived from that motion plus IsTrigger, so it is not
// authored here. The cooked-mesh shape used for static brush collision is added
// with the cook path (a CollisionShape asset handle alongside the primitive).
//=============================================================================
struct SENCHA_COMPONENT("sencha.physics.collider") Collider
{
    // A valid Mesh (a cooked shape resolved into the CollisionShapeCache, the
    // static-brush-collision case) takes precedence over the inline primitive
    // Shape used by authored/runtime colliders.
    CollisionShape Shape{};
    CollisionShapeHandle Mesh{};
    bool IsTrigger = false;
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/components/Collider.sencha.h>
#endif
