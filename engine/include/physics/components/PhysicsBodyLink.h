#pragma once

#include <ecs/ComponentAnnotations.h>
#include <physics/PhysicsTypes.h>

//=============================================================================
// PhysicsBodyLink
//
// Runtime-only link from a collider entity to its body in the shared
// PhysicsWorld. RigidBodyBinding's reconcile adds it when it creates a body and
// removes it when the body goes away. Never authored, never serialized, never
// cooked: it is rebuilt every run. Storing the handle in the chunk lets the
// per-frame transform sync walk (LocalTransform, RigidBody, PhysicsBodyLink) as
// contiguous columns with no per-entity lookup and no hashing.
//=============================================================================
struct SENCHA_COMPONENT("sencha.physics.body_link") PhysicsBodyLink
{
    PhysicsBodyId Body;
};

#if !defined(SENCHA_CODEGEN)
#  include <physics/components/PhysicsBodyLink.sencha.h>
#endif
