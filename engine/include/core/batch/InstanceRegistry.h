#pragma once
#include <core/raii/ILifetimeOwner.h>
#include <core/service/IService.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

//=============================================================================
// InstanceRegistry<T>
//
// Non-owning pointer registry for externally-lived instances of T. Maintains
// a contiguous array of active pointers so systems can sweep every instance
// of a type without walking the owning gameplay objects. The registry does
// NOT own the pointed-to objects — their lifetime is the caller's problem,
// and InstanceRegistryHandle only guarantees that Detach runs before the
// pointed-to object goes away.
//
// Use this when you want:
//   - O(1) registration / deregistration via Attach/Detach (ILifetimeOwner)
//   - Flat iteration over every live instance of T for a system pass
//   - Optional dirty flag for lazy resorting / filtering
//
// Do NOT use this when hot data (transforms, meshes, rigidbodies) should
// live in SoA form — that's what DataBatch<T> is for. InstanceRegistry<T>
// stores T* and is appropriate for objects whose storage already exists
// elsewhere (components on gameplay objects, subsystems, etc.).
//
// Usage:
//   services.AddService<InstanceRegistry<IRenderable>>();
//
//   // in the owning object:
//   InstanceRegistryHandle<IRenderable> handle(&registry, this);
//
//   // in a system:
//   for (auto* r : registry.GetItems())
//       r->Draw();
//=============================================================================
template<typename T>
class InstanceRegistry : public ILifetimeOwner, public IService
{
public:
	// -- ILifetimeOwner -----------------------------------------------------

	void Attach(uint64_t token) override
	{
		Add(reinterpret_cast<T*>(token));
	}

	void Detach(uint64_t token) override
	{
		Remove(reinterpret_cast<T*>(token));
	}

	// -- Direct typed API ---------------------------------------------------

	void Add(T* item)
	{
		assert(item != nullptr);

		auto it = IndexMap.find(item);
		if (it != IndexMap.end())
		{
			return; // Already added
		}

		size_t index = Items.size();
		Items.push_back(item);
		IndexMap[item] = index;
		bIsDirty = true;
	}

	void Remove(T* item)
	{
		auto it = IndexMap.find(item);
		if (it == IndexMap.end())
		{
			return; // Not registered
		}

		size_t indexToRemove = it->second;
		size_t lastIndex = Items.size() - 1;

		if (indexToRemove != lastIndex)
		{
			T* lastItem = Items[lastIndex];
			Items[indexToRemove] = lastItem;
			IndexMap[lastItem] = indexToRemove;
		}

		Items.pop_back();
		IndexMap.erase(it);
		bIsDirty = true;
	}

	// -- Queries ------------------------------------------------------------

	std::span<T* const> GetItems() const
	{
		return std::span<T* const>(Items.data(), Items.size());
	}

	std::span<T*> GetItemsMutable()
	{
		return std::span<T*>(Items.data(), Items.size());
	}

	bool Contains(T* item) const
	{
		return IndexMap.find(item) != IndexMap.end();
	}

	size_t Count() const { return Items.size(); }
	bool IsEmpty() const { return Items.empty(); }

	// -- Dirty tracking -----------------------------------------------------

	void MarkDirty() { bIsDirty = true; }

	bool CheckAndClearDirty()
	{
		bool wasDirty = bIsDirty;
		bIsDirty = false;
		return wasDirty;
	}

	template<typename Comparator>
	void SortIfDirty(Comparator comp)
	{
		if (!bIsDirty) return;

		std::sort(Items.begin(), Items.end(), comp);

		for (size_t i = 0; i < Items.size(); ++i)
		{
			IndexMap[Items[i]] = i;
		}

		bIsDirty = false;
	}

	// -- Housekeeping -------------------------------------------------------

	void Clear()
	{
		Items.clear();
		IndexMap.clear();
		bIsDirty = false;
	}

	// Range-based for loop support
	auto begin() const { return Items.begin(); }
	auto end() const { return Items.end(); }

private:
	std::vector<T*> Items;
	std::unordered_map<T*, size_t> IndexMap;
	bool bIsDirty = false;
};
