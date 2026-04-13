#pragma once

#include <batch/DataBatch.h>
#include <service/IService.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//=============================================================================
// TransformHierarchy2Service
//
// Owns the 2D transform graph — spatial parent/child relationships keyed
// by stable DataBatchKey. This is the TRANSFORM graph, not the node graph.
//
// Any spatial participant (SceneNode2D, tilemap, etc.) can register here
// as long as it has a DataBatchKey from the local transform service.
// The hierarchy stores non-owning keys only — transform lifetime is
// managed by whoever owns the LifetimeHandle.
//
// Design notes:
//   - SetParent / ClearParent are the mutation API.
//   - GetChildren returns an ordered child list for a given parent.
//   - GetRoots returns all keys that have no parent.
//   - The hierarchy is walked by TransformPropagation2System to derive
//     world transforms from local transforms.
//=============================================================================
class TransformHierarchy2Service : public IService
{
public:
	// -- Relationship mutation -----------------------------------------------

	// Set `child` as a spatial child of `parent`. Removes any prior parent.
	void SetParent(DataBatchKey child, DataBatchKey parent)
	{
		assert(child.Value != 0 && "Cannot parent a null key.");
		assert(parent.Value != 0 && "Cannot parent under a null key.");
		assert(child.Value != parent.Value && "Cannot parent to self.");

		ClearParent(child);

		ChildToParent[child.Value] = parent.Value;
		ParentToChildren[parent.Value].push_back(child.Value);
		EnsureRegistered(child);
		EnsureRegistered(parent);
	}

	// Remove `child` from its current parent. No-op if unparented.
	void ClearParent(DataBatchKey child)
	{
		auto it = ChildToParent.find(child.Value);
		if (it == ChildToParent.end()) return;

		uint32_t parentKey = it->second;
		ChildToParent.erase(it);

		auto pit = ParentToChildren.find(parentKey);
		if (pit != ParentToChildren.end())
		{
			auto& children = pit->second;
			children.erase(
				std::remove(children.begin(), children.end(), child.Value),
				children.end());
			if (children.empty())
				ParentToChildren.erase(pit);
		}
	}

	// Register a key as a known participant. Appears in GetRoots() if
	// it has no parent.
	void Register(DataBatchKey key)
	{
		assert(key.Value != 0 && "Cannot register a null key.");
		EnsureRegistered(key);
	}

	// Remove a key from the hierarchy entirely. Orphans its children
	// (they become roots). Call this when a participant is destroyed.
	void Unregister(DataBatchKey key)
	{
		ClearParent(key);

		auto cit = ParentToChildren.find(key.Value);
		if (cit != ParentToChildren.end())
		{
			for (uint32_t childKey : cit->second)
			{
				ChildToParent.erase(childKey);
			}
			ParentToChildren.erase(cit);
		}

		Registered.erase(key.Value);
	}

	// -- Queries ------------------------------------------------------------

	DataBatchKey GetParent(DataBatchKey child) const
	{
		auto it = ChildToParent.find(child.Value);
		if (it == ChildToParent.end()) return DataBatchKey{};
		return DataBatchKey{ it->second };
	}

	bool HasParent(DataBatchKey child) const
	{
		return ChildToParent.contains(child.Value);
	}

	const std::vector<uint32_t>& GetChildren(DataBatchKey parent) const
	{
		static const std::vector<uint32_t> Empty;
		auto it = ParentToChildren.find(parent.Value);
		if (it == ParentToChildren.end()) return Empty;
		return it->second;
	}

	bool HasChildren(DataBatchKey parent) const
	{
		auto it = ParentToChildren.find(parent.Value);
		return it != ParentToChildren.end() && !it->second.empty();
	}

	std::vector<DataBatchKey> GetRoots() const
	{
		std::vector<DataBatchKey> roots;
		for (uint32_t key : Registered)
		{
			if (!ChildToParent.contains(key))
				roots.push_back(DataBatchKey{ key });
		}
		return roots;
	}

	bool IsRegistered(DataBatchKey key) const
	{
		return Registered.contains(key.Value);
	}

	size_t Count() const { return Registered.size(); }

private:
	void EnsureRegistered(DataBatchKey key)
	{
		Registered.insert(key.Value);
	}

	// Non-owning: stores raw key values, not LifetimeHandles.
	std::unordered_map<uint32_t, uint32_t> ChildToParent;
	std::unordered_map<uint32_t, std::vector<uint32_t>> ParentToChildren;
	std::unordered_set<uint32_t> Registered;
};
