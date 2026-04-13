#pragma once

#include <batch/DataBatch.h>
#include <math/Transform2.h>
#include <system/ISystem.h>
#include <transform/TransformHierarchy2Service.h>

//=============================================================================
// TransformPropagation2System
//
// Derives world transforms from local transforms by walking the 2D
// transform hierarchy. Operates on the TRANSFORM graph, not on
// SceneNode2D or any specific object model.
//
// Root entries: world = local.
// Parented entries: world = parent_world * local.
//
// This system takes non-owning references to the local/world transform
// batches and hierarchy service. It does not own any data.
//=============================================================================
class TransformPropagation2System : public ISystem
{
public:
	TransformPropagation2System(
		DataBatch<Transform2f>& locals,
		DataBatch<Transform2f>& worlds,
		TransformHierarchy2Service& hierarchy)
		: Locals(locals)
		, Worlds(worlds)
		, Hierarchy(hierarchy)
	{
	}

	// Propagate can also be called manually (e.g., after spatial changes
	// outside the normal update loop).
	void Propagate()
	{
		auto roots = Hierarchy.GetRoots();
		for (DataBatchKey root : roots)
		{
			PropagateRecursive(root, Transform2f::Identity());
		}
	}

private:
	void Update() override
	{
		Propagate();
	}

	void PropagateRecursive(DataBatchKey key, const Transform2f& parentWorld)
	{
		const Transform2f* local = Locals.TryGet(key);
		if (!local) return;

		Transform2f world = parentWorld * (*local);

		Transform2f* worldSlot = Worlds.TryGet(key);
		if (worldSlot)
		{
			*worldSlot = world;
		}

		const auto& children = Hierarchy.GetChildren(key);
		for (uint32_t childKey : children)
		{
			PropagateRecursive(DataBatchKey{ childKey }, world);
		}
	}

	DataBatch<Transform2f>& Locals;
	DataBatch<Transform2f>& Worlds;
	TransformHierarchy2Service& Hierarchy;
};
