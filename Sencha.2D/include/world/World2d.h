#pragma once

#include <core/batch/DataBatch.h>
#include <math/geometry/2d/Transform2d.h>
#include <physics/2d/PhysicsDomain2D.h>
#include <physics/2d/RigidBody2D.h>
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
	{}

	PhysicsDomain2D        Physics;
	DataBatch<RigidBody2D> Bodies;
};
