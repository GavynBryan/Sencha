#pragma once

#include <core/service/IService.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <core/batch/DataBatch.h>
#include <physics/2d/PhysicsDomain2D.h>
#include <physics/2d/RigidBody2D.h>
#include <world/entity/EntityKey.h>
#include <world/entity/EntityRegistry.h>
#include <world/transform/TransformSpace.h>

//=============================================================================
// World<TTransform>
//
// Gameplay-facing service bundle for transform-dimensioned game-world state.
// World owns a TransformSpace (the self-contained transform space used by
// the game simulation) and an EntityRegistry, and registers with ServiceHost
// so gameplay code can resolve it by dimension.
//
// World is NOT the only TransformSpace in the engine. UI, editor gizmos, or
// any other subsystem that wants an isolated coordinate space creates its own
// TransformSpace directly — no World involvement, no service registration,
// no hierarchy conflicts with gameplay. World is simply "the domain that
// belongs to the game simulation."
//
// Each specialization is a distinct service type in ServiceHost (tagged by
// typeid), so World2d and World3d are resolved independently.
//=============================================================================
template <typename TTransform>
class World : public IService
{
public:
	World()
		: Transforms(Domain.Transforms)
		, TransformHierarchy(Domain.Hierarchy)
	{
	}

	// -- The transform space owned by this world ---------------------------

	TransformSpace<TTransform> Domain;

	// -- Entity registry ---------------------------------------------------

	EntityRegistry Entities;

	// Destroy an entity and all of its transform-hierarchy descendants,
	// leaves first. Convenience wrapper — passes Domain.Hierarchy so callers
	// don't have to.
	void DestroySubtree(EntityKey root)
	{
		Entities.DestroySubtree(root, Domain.Hierarchy);
	}

	// -- Gameplay-facing shortcuts (forward into Domain) -------------------

	TransformView<TTransform>& Transforms;
	TransformHierarchyService& TransformHierarchy;
};

//=============================================================================
// World2d
//
// Extends the base World with a PhysicsDomain2D. Physics is a separate concern
// from the transform/entity world, but it is scoped and owned by the 2D world —
// the same way TransformSpace is. PhysicsSetup2D wires systems against this
// member rather than registering a standalone service.
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

using World3d = World<Transform3f>;
