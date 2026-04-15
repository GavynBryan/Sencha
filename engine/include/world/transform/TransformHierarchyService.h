#pragma once

#include <core/batch/DataBatchKey.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

//=============================================================================
// TransformHierarchyService
//
// Owns a transform-domain graph: spatial parent/child relationships keyed by
// stable DataBatchKey. This is the TRANSFORM graph, not the node graph.
//
// Any spatial participant (scene node, tilemap, camera anchor, etc.) can
// register here as long as it has a DataBatchKey from the matching local
// transform service. The hierarchy stores non-owning keys only; transform
// lifetime is managed by whoever owns the DataBatchHandle.
//
//=============================================================================
class TransformHierarchyService
{
public:
	// -- Relationship mutation -----------------------------------------------

	// Set `child` as a spatial child of `parent`. Removes any prior parent.
	void SetParent(DataBatchKey child, DataBatchKey parent);

	// Remove `child` from its current parent. No-op if unparented.
	void ClearParent(DataBatchKey child);

	// Register a key as a known participant. Appears in GetRoots() if
	// it has no parent.
	void Register(DataBatchKey key);

	// Remove a key from the hierarchy entirely.
	// You MUST clear or unregister all children before unregistering a parent.
	// Call this when a participant is destroyed.
	void Unregister(DataBatchKey key);

	// -- Queries ------------------------------------------------------------

	DataBatchKey GetParent(DataBatchKey child) const;

	bool HasParent(DataBatchKey child) const;

	const std::vector<uint32_t>& GetChildren(DataBatchKey parent) const;

	bool HasChildren(DataBatchKey parent) const;

	std::vector<DataBatchKey> GetRoots() const;

	bool IsRegistered(DataBatchKey key) const;

	size_t Count() const;

	// Monotonically-increasing version counter. Incremented on any structural
	// change (SetParent, ClearParent, Register, Unregister). Consumers that
	// cache a derived propagation order use this to detect when their cache
	// is stale without paying a hash lookup per item.
	uint64_t GetVersion() const { return VersionCounter; }

private:
	void EnsureRegistered(DataBatchKey key);

	// Non-owning: stores raw key values, not LifetimeHandles.
	std::unordered_map<uint32_t, uint32_t> ChildToParent;
	std::unordered_map<uint32_t, std::vector<uint32_t>> ParentToChildren;
	std::unordered_set<uint32_t> Registered;
	uint64_t VersionCounter = 0;
};

inline void TransformHierarchyService::SetParent(DataBatchKey child, DataBatchKey parent)
{
	assert(child.Value != 0 && "Cannot parent a null key.");
	assert(parent.Value != 0 && "Cannot parent under a null key.");
	assert(child.Value != parent.Value && "Cannot parent to self.");

	ClearParent(child);

	ChildToParent[child.Value] = parent.Value;
	ParentToChildren[parent.Value].push_back(child.Value);
	EnsureRegistered(child);
	EnsureRegistered(parent);
	++VersionCounter;
}

inline void TransformHierarchyService::ClearParent(DataBatchKey child)
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
	++VersionCounter;
}

inline void TransformHierarchyService::Register(DataBatchKey key)
{
	assert(key.Value != 0 && "Cannot register a null key.");
	EnsureRegistered(key);
}

inline void TransformHierarchyService::Unregister(DataBatchKey key)
{
	assert(!HasChildren(key) && "Cannot unregister a parent with active children. Tear down children first.");
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
	++VersionCounter;
}

inline DataBatchKey TransformHierarchyService::GetParent(DataBatchKey child) const
{
	auto it = ChildToParent.find(child.Value);
	if (it == ChildToParent.end()) return DataBatchKey{};
	return DataBatchKey{ it->second };
}

inline bool TransformHierarchyService::HasParent(DataBatchKey child) const
{
	return ChildToParent.contains(child.Value);
}

inline const std::vector<uint32_t>& TransformHierarchyService::GetChildren(DataBatchKey parent) const
{
	static const std::vector<uint32_t> Empty;
	auto it = ParentToChildren.find(parent.Value);
	if (it == ParentToChildren.end()) return Empty;
	return it->second;
}

inline bool TransformHierarchyService::HasChildren(DataBatchKey parent) const
{
	auto it = ParentToChildren.find(parent.Value);
	return it != ParentToChildren.end() && !it->second.empty();
}

inline std::vector<DataBatchKey> TransformHierarchyService::GetRoots() const
{
	std::vector<DataBatchKey> roots;
	for (uint32_t key : Registered)
	{
		if (!ChildToParent.contains(key))
			roots.push_back(DataBatchKey{ key });
	}
	return roots;
}

inline bool TransformHierarchyService::IsRegistered(DataBatchKey key) const
{
	return Registered.contains(key.Value);
}

inline size_t TransformHierarchyService::Count() const
{
	return Registered.size();
}

inline void TransformHierarchyService::EnsureRegistered(DataBatchKey key)
{
	auto result = Registered.insert(key.Value);
	if (result.second)
		++VersionCounter;
}
