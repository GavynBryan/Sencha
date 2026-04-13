#pragma once

#include <batch/DataBatch.h>
#include <service/ServiceProvider.h>
#include <system/ISystem.h>
#include <transform/TransformHierarchyService.h>
#include <transform/TransformServiceTags.h>
#include <cstdint>
#include <vector>

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
// Propagate() is a single linear sweep over a cached flat order. Each entry
// carries resolved dense indices into the Locals and Worlds batches, plus the
// parent's dense index in Worlds (or NullIndex for roots). The hot loop does
// no hash lookups, no recursion, and no allocation.
//
// The cached order is rebuilt lazily when any of {hierarchy, locals, worlds}
// report a changed version counter. Rebuild does a BFS from hierarchy roots
// so that every child is emitted after its parent, which makes the sweep a
// single forward pass. The BFS is iterative, so deep hierarchies cannot blow
// the stack.
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

	static constexpr uint32_t NullIndex = UINT32_MAX;

	explicit TransformPropagationSystem(const ServiceProvider& services)
		: Locals(services.GetTagged<DataBatch<TTransform>, TLocalTag>())
		, Worlds(services.GetTagged<DataBatch<TTransform>, TWorldTag>())
		, Hierarchy(services.Get<HierarchyType>())
	{
	}

	// Propagate can also be called manually (e.g., after spatial changes
	// outside the normal update loop).
	void Propagate()
	{
		MaybeRebuildPropagationOrder();

		if (PropagationOrder.empty())
			return;

		std::span<TTransform> localsSpan = Locals.GetItems();
		std::span<TTransform> worldsSpan = Worlds.GetItems();

		for (const PropagationEntry& entry : PropagationOrder)
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

	struct PropagationEntry
	{
		uint32_t LocalIndex;
		uint32_t WorldIndex;
		uint32_t ParentWorldIndex;  // NullIndex for roots.
	};

	void MaybeRebuildPropagationOrder()
	{
		const uint64_t hierarchyVersion = Hierarchy.GetVersion();
		const uint64_t localsVersion = Locals.GetVersion();
		const uint64_t worldsVersion = Worlds.GetVersion();

		if (hierarchyVersion == CachedHierarchyVersion
			&& localsVersion == CachedLocalsVersion
			&& worldsVersion == CachedWorldsVersion)
		{
			return;
		}

		RebuildPropagationOrder();

		CachedHierarchyVersion = hierarchyVersion;
		CachedLocalsVersion = localsVersion;
		CachedWorldsVersion = worldsVersion;
	}

	void RebuildPropagationOrder()
	{
		PropagationOrder.clear();
		PropagationOrder.reserve(Worlds.Count());

		// Iterative BFS from roots so parents always precede children in the
		// emitted order. VisitQueue is reused between rebuilds to avoid
		// re-allocating on every hierarchy change.
		VisitQueue.clear();
		auto roots = Hierarchy.GetRoots();
		for (DataBatchKey root : roots)
			VisitQueue.push_back(root);

		for (size_t head = 0; head < VisitQueue.size(); ++head)
		{
			const DataBatchKey key = VisitQueue[head];

			const uint32_t localIdx = Locals.IndexOf(key);
			const uint32_t worldIdx = Worlds.IndexOf(key);

			// Preserve the original recursive behaviour: a registered key
			// that is missing from either batch is skipped along with its
			// subtree. Callers that need partial hierarchies can emplace
			// placeholder identity transforms.
			if (localIdx == NullIndex || worldIdx == NullIndex)
				continue;

			uint32_t parentWorldIdx = NullIndex;
			const DataBatchKey parentKey = Hierarchy.GetParent(key);
			if (parentKey.Value != 0)
				parentWorldIdx = Worlds.IndexOf(parentKey);

			PropagationOrder.push_back({ localIdx, worldIdx, parentWorldIdx });

			const std::vector<uint32_t>& children = Hierarchy.GetChildren(key);
			for (uint32_t childKey : children)
				VisitQueue.push_back(DataBatchKey{ childKey });
		}
	}

	DataBatch<TTransform>& Locals;
	DataBatch<TTransform>& Worlds;
	HierarchyType& Hierarchy;

	std::vector<PropagationEntry> PropagationOrder;
	std::vector<DataBatchKey> VisitQueue;

	uint64_t CachedHierarchyVersion = UINT64_MAX;
	uint64_t CachedLocalsVersion = UINT64_MAX;
	uint64_t CachedWorldsVersion = UINT64_MAX;
};
