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
// and rigid-body storage owned by the 2D library.
//=============================================================================
class World2d : public World<Transform2f>
{
public:
	explicit World2d(const PhysicsConfig2D& physicsConfig = {})
		: Physics(physicsConfig)
		, Bodies(Physics)
	{}

	PhysicsDomain2D        Physics;
	RigidBodyStore         Bodies;
	SpriteStore            Sprites;
};
