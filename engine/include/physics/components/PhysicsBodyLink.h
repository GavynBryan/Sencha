#pragma once

#include <ecs/ComponentTypeId.h>
#include <physics/PhysicsTypes.h>

//=============================================================================
// PhysicsBodyLink
//
// Runtime-only link from a collider entity to its body in the shared
// PhysicsWorld. PhysicsScene's reconcile adds it when it creates a body and
// removes it when the body goes away. Never authored, never serialized, never
// cooked: it is rebuilt every run. Storing the handle in the chunk lets the
// per-frame transform sync walk (LocalTransform, RigidBody, PhysicsBodyLink) as
// contiguous columns with no per-entity lookup and no hashing.
//=============================================================================
struct PhysicsBodyLink
{
    PhysicsBodyId Body;
};

SENCHA_DECLARE_COMPONENT_TYPE(PhysicsBodyLink, "sencha.physics.body_link");
