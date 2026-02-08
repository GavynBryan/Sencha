#pragma once
#include <service/IService.h>
#include <batch/IBatchArray.h>

//=============================================================================
// BatchArray<T>
// 
// Templated service that maintains a contiguous array of all active instances
// of a specific type. Enables ECS-style iteration patterns where systems
// iterate directly over typed arrays rather than all entities.
// 
// Features:
//   - O(1) add and duplicate checking
//   - O(1) removal via swap-and-pop
//   - Automatic cleanup via BatchArrayHandle RAII
//   - Optional dirty flag for lazy operations (sorting, filtering)
// 
// Usage:
//   // In game setup:
//   services.AddService<BatchArray<RigidBody2DComponent>>();
//   
//   // In component constructor:
//   handles.emplace_back(&services.Get<BatchArray<RigidBody2DComponent>>(), this);
//   
//   // In system:
//   for (auto* rb : services.Get<BatchArray<RigidBody2DComponent>>().GetItems())
//       ApplyGravity(rb);
//=============================================================================
template<typename T>
class BatchArray : public IBatchArray, public IService
{
public:
	// Type-erased add (called by BatchArrayHandle constructor)
	void Add(void* item) override
	{
		Add(static_cast<T*>(item));
	}

	// Type-erased remove (called by BatchArrayHandle destructor)
	void Remove(void* item) override
	{
		Remove(static_cast<T*>(item));
	}

	// Direct add (O(1) via index map)
	void Add(T* item)
	{
		assert(item != nullptr);
		
		// O(1) duplicate check via index map
		auto it = IndexMap.find(item);
		if (it != IndexMap.end())
		{
			return; // Already added
		}

		// Add to vector and track index
		size_t index = Items.size();
		Items.push_back(item);
		IndexMap[item] = index;
		bIsDirty = true;
	}

	// Direct remove (O(1) via swap-and-pop)
	void Remove(T* item)
	{
		auto it = IndexMap.find(item);
		if (it == IndexMap.end())
		{
			return; // Not in array
		}

		size_t indexToRemove = it->second;
		size_t lastIndex = Items.size() - 1;

		// Swap with last element if not already last
		if (indexToRemove != lastIndex)
		{
			T* lastItem = Items[lastIndex];
			Items[indexToRemove] = lastItem;
			IndexMap[lastItem] = indexToRemove;
		}

		// Remove last element
		Items.pop_back();
		IndexMap.erase(it);
		bIsDirty = true;
	}

	// Get all items (read-only span for safe iteration)
	std::span<T* const> GetItems() const
	{
		return std::span<T* const>(Items.data(), Items.size());
	}

	// Get mutable access to items (use with care)
	std::span<T*> GetItemsMutable()
	{
		return std::span<T*>(Items.data(), Items.size());
	}

	// Check if an item is in the array
	bool Contains(T* item) const
	{
		return IndexMap.find(item) != IndexMap.end();
	}

	// Get number of items
	size_t Count() const { return Items.size(); }

	// Check if empty
	bool IsEmpty() const { return Items.empty(); }

	// Mark array as dirty (for use with lazy operations like sorting)
	void MarkDirty() override { bIsDirty = true; }

	// Check and clear dirty flag
	bool CheckAndClearDirty()
	{
		bool wasDirty = bIsDirty;
		bIsDirty = false;
		return wasDirty;
	}

	// Sort items using a comparator (only if dirty)
	template<typename Comparator>
	void SortIfDirty(Comparator comp)
	{
		if (!bIsDirty) return;

		std::sort(Items.begin(), Items.end(), comp);
		
		// Rebuild index map after sort
		for (size_t i = 0; i < Items.size(); ++i)
		{
			IndexMap[Items[i]] = i;
		}
		
		bIsDirty = false;
	}

	// Clear all items (does NOT notify items)
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
	std::unordered_map<T*, size_t> IndexMap;  // Item -> index for O(1) lookup/removal
	bool bIsDirty = false;
};