#pragma once

#include <core/batch/DataBatch.h>
#include <world/transform/TransformHierarchyService.h>
#include <world/transform/TransformPropagationOrderService.h>
#include <cstdint>
#include <span>

//=============================================================================
// TransformPropagationSystem
//
// Derives world transforms from local transforms by walking a transform-domain
// hierarchy. Operates on the TRANSFORM graph, not on any specific object model.
//
// Root entries: world = local.
// Parented entries: world = parent_world * local.
//
// TTransform must provide:
//   - static TTransform Identity()
//   - TTransform operator*(const TTransform&) const
//
// Implementation notes
// --------------------
// Propagate() is a single linear sweep over a service-owned cached flat order.
// Each entry carries resolved dense indices into the Locals and Worlds batches,
// plus the parent's dense index in Worlds (or NullIndex for roots). The hot loop
// does no hash lookups, no recursion, and no allocation.
//
// The service-owned cached order is rebuilt lazily when any of {hierarchy,
// locals, worlds} report a changed version counter. Rebuild does a BFS from
// hierarchy roots so that every child is emitted after its parent, which makes
// the sweep a single forward pass. The BFS is iterative, so deep hierarchies
// cannot blow the stack.
//=============================================================================
template <typename TTransform>
class TransformPropagationSystem
{
public:
	using TransformType = TTransform;
	using HierarchyType = TransformHierarchyService;
	using CacheType = TransformPropagationOrderService;
	using PropagationEntry = typename CacheType::PropagationEntry;

	static constexpr uint32_t NullIndex = CacheType::NullIndex;

	TransformPropagationSystem(
		DataBatch<TTransform>& locals,
		DataBatch<TTransform>& worlds,
		HierarchyType& hierarchy,
		CacheType& cache)
		: Locals(locals)
		, Worlds(worlds)
		, Hierarchy(hierarchy)
		, Cache(cache)
	{
	}

	// Propagate can also be called manually (e.g., after spatial changes
	// outside the normal update loop).
	void Propagate()
	{
		Cache.MaybeRebuild(Hierarchy, Locals, Worlds);

		std::span<const PropagationEntry> order = Cache.GetOrder();
		if (order.empty())
			return;

		std::span<const TTransform> localsSpan = Locals.GetItems();
		std::span<TTransform> worldsSpan = Worlds.GetItems();

		for (const PropagationEntry& entry : order)
		{
			const TTransform& local = localsSpan[entry.LocalIndex];
			if (entry.ParentWorldIndex == NullIndex)
			{
				worldsSpan[entry.WorldIndex] = local;
			}
			else
			{
				worldsSpan[entry.WorldIndex] =
					worldsSpan[entry.ParentWorldIndex] * local;
			}
		}
	}

	void Tick(float /*fixedDt*/)
	{
		Propagate();
	}

private:
	DataBatch<TTransform>& Locals;
	DataBatch<TTransform>& Worlds;
	HierarchyType& Hierarchy;
	CacheType& Cache;
};
