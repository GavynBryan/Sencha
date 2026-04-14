#pragma once

#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <world/transform/core/TransformServiceTags.h>
#include <world/transform/hierarchy/TransformHierarchyService.h>
#include <core/service/IService.h>
#include <cstdint>
#include <span>
#include <vector>

//=============================================================================
// TransformPropagationOrderService
//
// Owns the cached flat propagation order derived from a transform hierarchy and
// matching local/world transform batches. Systems can be destroyed and recreated
// without losing the derived order or the scratch queue used to rebuild it.
//
// TDomainTag selects the hierarchy service. TLocalTag and TWorldTag select the
// local/world DataBatch roles that this cache was built against.
//=============================================================================
template <
	typename TDomainTag,
	typename TLocalTag = TransformServiceTags::LocalTransformTag,
	typename TWorldTag = TransformServiceTags::WorldTransformTag>
class TransformPropagationOrderService : public IService
{
public:
	using DomainTag = TDomainTag;
	using LocalTag = TLocalTag;
	using WorldTag = TWorldTag;
	using HierarchyType = TransformHierarchyService<TDomainTag>;

	static constexpr uint32_t NullIndex = UINT32_MAX;

	struct PropagationEntry
	{
		uint32_t LocalIndex;
		uint32_t WorldIndex;
		uint32_t ParentWorldIndex;  // NullIndex for roots.
	};

	template <typename TTransform>
	void MaybeRebuild(
		const HierarchyType& hierarchy,
		const DataBatch<TTransform>& locals,
		const DataBatch<TTransform>& worlds)
	{
		const uint64_t hierarchyVersion = hierarchy.GetVersion();
		const uint64_t localsVersion = locals.GetVersion();
		const uint64_t worldsVersion = worlds.GetVersion();

		if (hierarchyVersion == CachedHierarchyVersion
			&& localsVersion == CachedLocalsVersion
			&& worldsVersion == CachedWorldsVersion)
		{
			return;
		}

		Rebuild(hierarchy, locals, worlds);

		CachedHierarchyVersion = hierarchyVersion;
		CachedLocalsVersion = localsVersion;
		CachedWorldsVersion = worldsVersion;
	}

	std::span<const PropagationEntry> GetOrder() const
	{
		return std::span<const PropagationEntry>(Order);
	}

private:
	// Carries the resolved parent world-index alongside the key, so Rebuild
	// never has to call Hierarchy.GetParent or Worlds.IndexOf(parentKey): the
	// parent's world-index is already known when the child is enqueued.
	struct QueueEntry
	{
		DataBatchKey Key;
		uint32_t ParentWorldIndex;  // NullIndex for roots.
	};

	template <typename TTransform>
	void Rebuild(
		const HierarchyType& hierarchy,
		const DataBatch<TTransform>& locals,
		const DataBatch<TTransform>& worlds)
	{
		Order.clear();
		Order.reserve(worlds.Count());

		// Iterative BFS from roots so parents always precede children in the
		// emitted order. VisitQueue is reused between rebuilds to avoid
		// re-allocating on every hierarchy change.
		VisitQueue.clear();
		auto roots = hierarchy.GetRoots();
		for (DataBatchKey root : roots)
			VisitQueue.push_back({ root, NullIndex });

		for (size_t head = 0; head < VisitQueue.size(); ++head)
		{
			const auto [key, parentWorldIdx] = VisitQueue[head];

			const uint32_t localIdx = locals.IndexOf(key);
			const uint32_t worldIdx = worlds.IndexOf(key);

			// Preserve the original recursive behaviour: a registered key
			// that is missing from either batch is skipped along with its
			// subtree. Callers that need partial hierarchies can emplace
			// placeholder identity transforms.
			if (localIdx == NullIndex || worldIdx == NullIndex)
				continue;

			Order.push_back({ localIdx, worldIdx, parentWorldIdx });

			const std::vector<uint32_t>& children = hierarchy.GetChildren(key);
			for (uint32_t childKey : children)
				VisitQueue.push_back({ DataBatchKey{ childKey }, worldIdx });
		}
	}

	std::vector<PropagationEntry> Order;
	std::vector<QueueEntry> VisitQueue;

	uint64_t CachedHierarchyVersion = UINT64_MAX;
	uint64_t CachedLocalsVersion = UINT64_MAX;
	uint64_t CachedWorldsVersion = UINT64_MAX;
};
