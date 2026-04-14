#pragma once

#include <core/batch/DataBatch.h>
#include <world/transform/core/TransformServiceTags.h>
#include <world/transform/hierarchy/TransformHierarchyService.h>
#include <world/transform/hierarchy/TransformPropagationOrderService.h>
#include <core/service/ServiceProvider.h>
#include <core/system/ISystem.h>
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
// TDomainTag selects the hierarchy service. TLocalTag and TWorldTag select the
// local/world DataBatch roles inside ServiceHost.
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
template <
	typename TTransform,
	typename TDomainTag,
	typename TLocalTag = TransformServiceTags::LocalTransformTag,
	typename TWorldTag = TransformServiceTags::WorldTransformTag>
class TransformPropagationSystem : public ISystem
{
public:
	using TransformType = TTransform;
	using DomainTag = TDomainTag;
	using LocalTag = TLocalTag;
	using WorldTag = TWorldTag;
	using HierarchyType = TransformHierarchyService<TDomainTag>;
	using CacheType = TransformPropagationOrderService<
		TDomainTag,
		TLocalTag,
		TWorldTag>;
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

	explicit TransformPropagationSystem(const ServiceProvider& services)
		: TransformPropagationSystem(
			services.GetTagged<DataBatch<TTransform>, TLocalTag>(),
			services.GetTagged<DataBatch<TTransform>, TWorldTag>(),
			services.Get<HierarchyType>(),
			services.Get<CacheType>())
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

private:
	void Update() override
	{
		Propagate();
	}

	DataBatch<TTransform>& Locals;
	DataBatch<TTransform>& Worlds;
	HierarchyType& Hierarchy;
	CacheType& Cache;
};
