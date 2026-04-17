#pragma once

#include <core/service/IService.h>
#include <entity/EntityRegistry.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformPropagationOrderService.h>
#include <transform/TransformStore.h>
#include <world/ComponentRegistry.h>

//=============================================================================
// World<TTransform>
//
// Gameplay-facing service bundle for transform-dimensioned game-world state.
// Owns the transform triad (Hierarchy, PropagationOrder, Transforms), an
// EntityRegistry, and a ComponentRegistry for extensible component stores.
//
// World is NOT the only transform domain in the engine. UI, editor gizmos, or
// any other subsystem that wants an isolated coordinate space creates its own
// TransformSpace directly — no World involvement, no service registration,
// no hierarchy conflicts with gameplay. World is simply "the domain that
// belongs to the game simulation."
//
// Dimension-specific libraries derive concrete world services from this
// template without making Core depend on their physics or rendering systems.
//=============================================================================
template <typename TTransform>
class World : public IService
{
public:
	World()
		: Transforms(PropagationOrder)
	{}

	// -- Transform space ---------------------------------------------------

	TransformHierarchyService        Hierarchy;
	TransformPropagationOrderService PropagationOrder;
	TransformStore<TTransform>       Transforms;

	// -- Entity registry ---------------------------------------------------

	EntityRegistry Entities;

	// -- Component stores --------------------------------------------------
	// Engine modules and game code register stores here. Systems resolve a
	// typed pointer once at init and cache it — zero overhead in hot paths.

	ComponentRegistry Components;

	// Destroy an entity and all of its transform-hierarchy descendants,
	// leaves first.
	void DestroySubtree(EntityHandle root)
	{
		Entities.DestroySubtree(root, Hierarchy);
	}
};
