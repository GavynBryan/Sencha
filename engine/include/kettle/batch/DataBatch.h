#pragma once
#include <kettle/raii/ILifetimeOwner.h>
#include <kettle/raii/LifetimeHandle.h>
#include <kettle/service/IService.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <numeric>
#include <span>
#include <unordered_map>
#include <vector>

//=============================================================================
// DataBatchKey
//
// Strongly-typed key that identifies an item inside a DataBatch.
// Default-constructed keys have Value == 0, which LifetimeHandle treats
// as the null sentinel (TokenT{} is "invalid").
//=============================================================================
struct DataBatchKey
{
	uint32_t Value = 0;
	bool operator==(const DataBatchKey&) const = default;
};

//=============================================================================
// DataBatch<T>
//
// Templated service that OWNS a contiguous array of T values. This is the
// Data-Oriented Design counterpart to RefBatch: where RefBatch stores
// pointers to externally-owned objects, DataBatch stores the objects
// themselves in a cache-friendly, tightly packed vector<T>.
//
// Items are created via Emplace(), which returns a
// LifetimeHandle<DataBatchKey>. When the handle is destroyed (or reset),
// the corresponding item is removed from the batch using swap-and-pop to
// maintain contiguity.
//
// Internally, each item is assigned a stable DataBatchKey so that
// swap-and-pop doesn't invalidate handles. The key is carried by the
// typed LifetimeHandle — no void* visible in the public API.
//
// Usage:
//   DataBatch<Particle> particles;
//
//   auto h = particles.Emplace(position, velocity, lifetime);
//   // h is a LifetimeHandle<DataBatchKey>; destruction removes the particle.
//
//   for (auto& p : particles)        // cache-friendly value iteration
//       p.lifetime -= dt;
//
//   auto* p = particles.TryGet(h);   // random access by handle
//=============================================================================
template<typename T>
class DataBatch : public ILifetimeOwner, public IService
{
	static_assert(std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>,
		"DataBatch<T> requires T to be at least move-constructible.");

public:
	// -- Emplacement (the primary way to add data) --------------------------

	template<typename... Args>
	LifetimeHandle<DataBatchKey> Emplace(Args&&... args)
	{
		DataBatchKey key{ NextKey++ };

		Items.emplace_back(std::forward<Args>(args)...);
		IndexToKey.push_back(key.Value);
		KeyToIndex[key.Value] = Items.size() - 1;

		bIsDirty = true;

		// Use NoAttach — the item is already in the batch.
		return LifetimeHandle<DataBatchKey>(
			this, key, LifetimeHandle<DataBatchKey>::NoAttach);
	}

	// -- Random access by handle --------------------------------------------

	T* TryGet(const LifetimeHandle<DataBatchKey>& handle)
	{
		uint32_t key = handle.GetToken().Value;
		auto it = KeyToIndex.find(key);
		if (it == KeyToIndex.end()) return nullptr;
		return &Items[it->second];
	}

	const T* TryGet(const LifetimeHandle<DataBatchKey>& handle) const
	{
		uint32_t key = handle.GetToken().Value;
		auto it = KeyToIndex.find(key);
		if (it == KeyToIndex.end()) return nullptr;
		return &Items[it->second];
	}

	// -- Contiguous iteration (the whole point of DOD) ----------------------

	std::span<T> GetItems() { return std::span<T>(Items); }
	std::span<const T> GetItems() const { return std::span<const T>(Items); }

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

		// Build a permutation index so we can sort Items, IndexToKey, and
		// KeyToIndex in lockstep.
		std::vector<size_t> perm(Items.size());
		std::iota(perm.begin(), perm.end(), size_t{0});

		std::sort(perm.begin(), perm.end(), [&](size_t a, size_t b)
		{
			return comp(Items[a], Items[b]);
		});

		// Apply the permutation
		std::vector<T> sortedItems;
		std::vector<uint32_t> sortedKeys;
		sortedItems.reserve(Items.size());
		sortedKeys.reserve(Items.size());

		for (size_t i : perm)
		{
			sortedItems.push_back(std::move(Items[i]));
			sortedKeys.push_back(IndexToKey[i]);
		}

		Items = std::move(sortedItems);
		IndexToKey = std::move(sortedKeys);

		// Rebuild KeyToIndex
		for (size_t i = 0; i < Items.size(); ++i)
		{
			KeyToIndex[IndexToKey[i]] = i;
		}

		bIsDirty = false;
	}

	// -- Housekeeping -------------------------------------------------------

	void Clear()
	{
		Items.clear();
		IndexToKey.clear();
		KeyToIndex.clear();
		bIsDirty = false;
	}

	// Range-based for loop support (iterates over T values)
	auto begin() { return Items.begin(); }
	auto end() { return Items.end(); }
	auto begin() const { return Items.begin(); }
	auto end() const { return Items.end(); }

protected:
	// -- ILifetimeOwner -----------------------------------------------------

	// Attach is a no-op for DataBatch — items are added by Emplace().
	void Attach(void* /*token*/) override {}

	// Detach removes the item identified by the encoded key.
	void Detach(void* token) override
	{
		// For value-type tokens, LifetimeHandle passes &Token.
		uint32_t key = static_cast<DataBatchKey*>(token)->Value;
		auto it = KeyToIndex.find(key);
		if (it == KeyToIndex.end()) return;

		size_t indexToRemove = it->second;
		size_t lastIndex = Items.size() - 1;

		if (indexToRemove != lastIndex)
		{
			// Swap with last for O(1) removal
			Items[indexToRemove] = std::move(Items[lastIndex]);
			uint32_t lastKey = IndexToKey[lastIndex];
			IndexToKey[indexToRemove] = lastKey;
			KeyToIndex[lastKey] = indexToRemove;
		}

		Items.pop_back();
		IndexToKey.pop_back();
		KeyToIndex.erase(it);
		bIsDirty = true;
	}

private:
	std::vector<T> Items;                          // Dense, contiguous data
	std::vector<uint32_t> IndexToKey;              // Dense index → stable key
	std::unordered_map<uint32_t, size_t> KeyToIndex; // Stable key → dense index
	uint32_t NextKey = 1;                          // Start at 1: key 0 == DataBatchKey{} == "invalid"
	bool bIsDirty = false;
};
