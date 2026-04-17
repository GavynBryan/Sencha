#pragma once

#include <math/geometry/2d/Transform2d.h>
#include <physics/PhysicsDomain2D.h>
#include <physics/RigidBody2D.h>
#include <sprite/SpriteComponent.h>
#include <world/World.h>

//=============================================================================
// World2d
//
// 2D gameplay world. Extends the Core world template with the 2D physics domain
// and pre-registers the engine's 2D component stores into ComponentRegistry.
//
// Engine stores are exposed as named references for ergonomic access:
//   world.Bodies, world.Sprites
//
// These references are bound once at construction — hot-path access is a
// direct dereference, not a registry lookup.
//
// Game code registers additional stores through the inherited registry:
//   auto& health = world.Components.Register<HealthStore>();
//
// Systems resolve stores once at init and cache the typed pointer:
//   HealthStore* health = world.Components.Get<HealthStore>();
//=============================================================================
class World2d : public World<Transform2f>
{
public:
    explicit World2d(const PhysicsConfig2D& physicsConfig = {})
        : Physics(physicsConfig)
        , Bodies(Components.Register<RigidBodyStore>(Physics))
        , Sprites(Components.Register<SpriteStore>())
    {}

    PhysicsDomain2D  Physics;
    RigidBodyStore&  Bodies;
    SpriteStore&     Sprites;
};
