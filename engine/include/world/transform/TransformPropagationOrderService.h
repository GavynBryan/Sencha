#pragma once

#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <world/transform/TransformHierarchyService.h>
#include <cstdint>
#include <span>
#include <vector>

// Minimal dense bitset used by TransformPropagationOrderService.
// Indexed by the dense position in the propagation Order array.
struct DenseBitset
{
    void Resize(size_t count)
    {
        const size_t words = (count + 63) / 64;
        Words.assign(words, ~uint64_t{0}); // all set = all dirty after resize
    }

    void SetAll()
    {
        for (auto& w : Words) w = ~uint64_t{0};
    }

    void ClearAll()
    {
        for (auto& w : Words) w = 0;
    }

    void Set(size_t index)
    {
        Words[index >> 6] |= uint64_t{1} << (index & 63);
    }

    void Clear(size_t index)
    {
        Words[index >> 6] &= ~(uint64_t{1} << (index & 63));
    }

    bool Test(size_t index) const
    {
        return (Words[index >> 6] >> (index & 63)) & 1;
    }

    bool IsAllClear() const
    {
        for (auto w : Words) if (w) return false;
        return true;
    }

    std::vector<uint64_t> Words;
};

//=============================================================================
// TransformPropagationOrderService
//
// Owns the cached flat propagation order derived from a transform hierarchy and
// matching local/world transform batches. Systems can be destroyed and recreated
// without losing the derived order or the scratch queue used to rebuild it.
//
//=============================================================================
class TransformPropagationOrderService
{
public:
	using HierarchyType = TransformHierarchyService;

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

	// Mark the local transform at dense index `localIndex` as modified.
	// Called by TransformView whenever a mutable local pointer is handed out.
	void MarkLocalDirty(uint32_t localIndex)
	{
		if (localIndex < LocalDirty.Words.size() * 64)
			LocalDirty.Set(localIndex);
	}

	// True if no locals have been dirtied since the last Propagate cleared them.
	bool IsAllClean() const { return LocalDirty.IsAllClear(); }

	// Read access for the propagation sweep.
	const DenseBitset& GetLocalDirty()   const { return LocalDirty;   }
	      DenseBitset& GetLocalDirty()         { return LocalDirty;   }
	const DenseBitset& GetWorldChanged() const { return WorldChanged; }
	      DenseBitset& GetWorldChanged()       { return WorldChanged; }

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

		// After a structural rebuild every world transform needs recomputing.
		// Sized by batch count (not Order.size()) because entries are indexed
		// by their dense slot in the locals/worlds batches respectively.
		LocalDirty.Resize(locals.Count());   // all bits set
		WorldChanged.Resize(worlds.Count()); // all bits set (triggers full sweep once)
	}

	std::vector<PropagationEntry> Order;
	std::vector<QueueEntry> VisitQueue;

	// Dirty tracking. LocalDirty is set by TransformView on every mutable
	// local access; cleared by Propagate after the sweep. WorldChanged is
	// set by Propagate when a world transform is written; cleared at the
	// start of each sweep so children can see which parents changed THIS frame.
	DenseBitset LocalDirty;
	DenseBitset WorldChanged;

	uint64_t CachedHierarchyVersion = UINT64_MAX;
	uint64_t CachedLocalsVersion = UINT64_MAX;
	uint64_t CachedWorldsVersion = UINT64_MAX;
};
