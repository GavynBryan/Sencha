#pragma once

#include <core/service/IService.h>
#include <math/geometry/2d/Transform2d.h>
#include <math/geometry/3d/Transform3d.h>
#include <world/entity/EntityKey.h>
#include <world/entity/EntityRegistry.h>
#include <transform/TransformSpace.h>

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
// Dimension-specific libraries can derive concrete world services from this
// template without making Core depend on their physics or rendering systems.
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

	TransformStore<TTransform>& Transforms;
	TransformHierarchyService& TransformHierarchy;
};
