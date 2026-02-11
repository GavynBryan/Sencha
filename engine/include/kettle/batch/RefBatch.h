#pragma once
#include <kettle/raii/ILifetimeOwner.h>
#include <kettle/service/IService.h>
#include <algorithm>
#include <cassert>
#include <span>
#include <unordered_map>
#include <vector>

//=============================================================================
// RefBatch<T>
//
// Templated service that maintains a contiguous array of pointers to all
// active instances of a specific type. Does NOT own the pointed-to objects.
// Enables ECS-style iteration patterns where systems iterate directly over
// typed arrays rather than all entities.
//
// Implements ILifetimeOwner so that LifetimeHandle (or RefBatchHandle)
// can manage registration via RAII.
//
// Features:
//   - O(1) add and duplicate checking
//   - O(1) removal via swap-and-pop
//   - Automatic cleanup via RefBatchHandle RAII
//   - Optional dirty flag for lazy operations (sorting, filtering)
//
// Usage:
//   // In game setup:
//   services.AddService<RefBatch<RigidBody2DComponent>>();
//
//   // In component constructor:
//   handles.emplace_back(&services.Get<RefBatch<RigidBody2DComponent>>(), this);
//
//   // In system:
//   for (auto* rb : services.Get<RefBatch<RigidBody2DComponent>>().GetItems())
//       ApplyGravity(rb);
//=============================================================================
template<typename T>
class RefBatch : public ILifetimeOwner, public IService
{
public:
	// -- ILifetimeOwner -----------------------------------------------------

	void Attach(void* token) override
	{
		Add(static_cast<T*>(token));
	}

	void Detach(void* token) override
	{
		Remove(static_cast<T*>(token));
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
			return; // Not in batch
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
