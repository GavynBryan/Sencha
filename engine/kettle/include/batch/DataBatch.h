#pragma once
#include <batch/DataBatchKey.h>
#include <raii/DataBatchHandle.h>
#include <service/IService.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

//=============================================================================
// DataBatch<T>
//
// Templated service that OWNS a contiguous array of T values. This is the
// Data-Oriented Design counterpart to RefBatch: where RefBatch stores
// pointers to externally-owned objects, DataBatch stores the objects
// themselves in a cache-friendly, tightly packed vector<T>.
//
// Items are created via Emplace(), which returns a DataBatchHandle<T>.
// When the handle is destroyed (or reset),
// the corresponding item is removed from the batch using swap-and-pop to
// maintain contiguity.
//
// Internally, each item is assigned a stable DataBatchKey so that
// swap-and-pop doesn't invalidate handles. The key is carried by the typed
// DataBatchHandle<T>.
//
// Usage:
//   DataBatch<Particle> particles;
//
//   auto h = particles.Emplace(position, velocity, lifetime);
//   // h is a DataBatchHandle<Particle>; destruction removes the particle.
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
	DataBatchHandle<T> Emplace(Args&&... args)
	{
		if (NextKey == std::numeric_limits<uint32_t>::max())
			throw std::length_error("DataBatch key range exhausted.");

		DataBatchKey key{ NextKey };
		EnsureKeyIndexCapacity(key.Value);

		const size_t oldSize = Items.size();
		try
		{
			Items.emplace_back(std::forward<Args>(args)...);
			IndexToKey.push_back(key.Value);
			SetKeyIndex(key.Value, Items.size() - 1);
		}
		catch (...)
		{
			RollbackEmplace(oldSize, NextKey);
			throw;
		}

		++NextKey;

		bIsDirty = true;
		++VersionCounter;

		// Use NoAttach — the item is already in the batch.
		return DataBatchHandle<T>(
			this, key, DataBatchHandle<T>::NoAttach);
	}

	template<typename Factory>
	DataBatchBlock EmplaceBlock(size_t count, Factory&& factory)
	{
		if (count == 0)
			return DataBatchBlock{};

		if (count > static_cast<size_t>(std::numeric_limits<uint32_t>::max() - NextKey))
			throw std::length_error("DataBatch key range exhausted.");

		const size_t oldSize = Items.size();
		const uint32_t oldNextKey = NextKey;
		const uint32_t firstKey = NextKey;
		const uint32_t lastKey = firstKey + static_cast<uint32_t>(count - 1);
		Reserve(Items.size() + count);
		EnsureKeyIndexCapacity(lastKey);

		try
		{
			for (size_t i = 0; i < count; ++i)
			{
				decltype(auto) item = factory(i);
				Items.emplace_back(std::forward<decltype(item)>(item));
			}

			for (size_t i = 0; i < count; ++i)
			{
				IndexToKey.push_back(firstKey + static_cast<uint32_t>(i));
			}

			for (size_t i = 0; i < count; ++i)
			{
				SetKeyIndex(
					firstKey + static_cast<uint32_t>(i),
					oldSize + i);
			}
		}
		catch (...)
		{
			RollbackEmplace(oldSize, oldNextKey);
			throw;
		}

		NextKey = firstKey + static_cast<uint32_t>(count);
		bIsDirty = true;
		++VersionCounter;

		return DataBatchBlock{ firstKey, count };
	}

	void RemoveBlock(DataBatchBlock block)
	{
		if (block.IsEmpty() || Items.empty())
			return;

		if (block.Count == Items.size() && BlockCoversAllItems(block))
		{
			ClearItemsForRemoval();
			return;
		}

		RemoveKeyRange(block.FirstKey, block.Count);
	}

	void RemoveKeys(std::span<const DataBatchKey> keys)
	{
		RemoveKeySpan(keys);
	}

	void RemoveHandles(std::span<DataBatchHandle<T>> handles)
	{
		if (!Items.empty() && handles.size() == Items.size())
		{
			std::vector<uint8_t> covered(Items.size(), uint8_t{ 0 });
			size_t coveredCount = 0;
			bool coversWholeBatch = true;

			for (const auto& handle : handles)
			{
				if (handle.Owner != this || handle.Token == DataBatchKey{})
				{
					coversWholeBatch = false;
					break;
				}

				const uint32_t index = FindIndex(handle.Token);
				if (index == InvalidIndex)
				{
					coversWholeBatch = false;
					break;
				}

				uint8_t& isCovered = covered[index];
				if (isCovered == 0)
				{
					isCovered = 1;
					++coveredCount;
				}
			}

			if (coversWholeBatch && coveredCount == Items.size())
			{
				ClearItemsForRemoval();

				for (auto& handle : handles)
				{
					handle.Owner = nullptr;
					handle.Token = DataBatchKey{};
				}
				return;
			}
		}

		std::vector<DataBatchKey> keys;
		keys.reserve(handles.size());

		for (const auto& handle : handles)
		{
			if (handle.Owner == this && !(handle.Token == DataBatchKey{}))
				keys.push_back(handle.Token);
		}

		RemoveKeySpan(keys);

		for (auto& handle : handles)
		{
			if (handle.Owner == this)
			{
				handle.Owner = nullptr;
				handle.Token = DataBatchKey{};
			}
		}
	}

	// -- Random access by handle --------------------------------------------

	T* TryGet(const DataBatchHandle<T>& handle)
	{
		return TryGet(handle.GetToken());
	}

	const T* TryGet(const DataBatchHandle<T>& handle) const
	{
		return TryGet(handle.GetToken());
	}

	// -- Random access by raw key -------------------------------------------
	//
	// For non-owning lookups — e.g., the transform hierarchy stores
	// DataBatchKey values without owning the transform lifetime.

	T* TryGet(DataBatchKey key)
	{
		const uint32_t index = FindIndex(key);
		if (index == InvalidIndex) return nullptr;
		return &Items[index];
	}

	const T* TryGet(DataBatchKey key) const
	{
		const uint32_t index = FindIndex(key);
		if (index == InvalidIndex) return nullptr;
		return &Items[index];
	}

	bool Contains(DataBatchKey key) const
	{
		return FindIndex(key) != InvalidIndex;
	}

	// Returns the dense index for `key`, or UINT32_MAX if the key is not present.
	// Use this when you need to cache direct index access into GetItems() without
	// paying a hash lookup on every read.
	uint32_t IndexOf(DataBatchKey key) const
	{
		return FindIndex(key);
	}

	// Monotonically-increasing version counter. Incremented on any structural
	// change (Emplace, Detach, SortIfDirty, Clear). Non-destructive — callers
	// cache the last-seen value and compare to detect staleness. Unlike
	// CheckAndClearDirty, reading this does not affect any other observer.
	uint64_t GetVersion() const { return VersionCounter; }

	void Reserve(size_t capacity)
	{
		Items.reserve(capacity);
		IndexToKey.reserve(capacity);

		const size_t additionalKeyCapacity = capacity > Items.size()
			? capacity - Items.size()
			: 0;
		if (additionalKeyCapacity > 0)
		{
			KeyToIndex.reserve(
				static_cast<size_t>(NextKey) + additionalKeyCapacity);
		}
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

		// Build a permutation index so we can sort Items and IndexToKey in lockstep.
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

		// Rebuild KeyToIndex for the new dense order.
		for (size_t i = 0; i < Items.size(); ++i)
		{
			SetKeyIndex(IndexToKey[i], i);
		}

		bIsDirty = false;
		++VersionCounter;
	}

	// -- Housekeeping -------------------------------------------------------

	void Clear()
	{
		Items.clear();
		IndexToKey.clear();
		KeyToIndex.clear();
		bIsDirty = false;
		++VersionCounter;
	}

	// Range-based for loop support (iterates over T values)
	auto begin() { return Items.begin(); }
	auto end() { return Items.end(); }
	auto begin() const { return Items.begin(); }
	auto end() const { return Items.end(); }

protected:
	// -- ILifetimeOwner -----------------------------------------------------

	// Attach is a no-op for DataBatch — items are added by Emplace().
	void Attach(uint64_t /*token*/) override {}

	// Detach removes the item identified by the encoded key.
	void Detach(uint64_t token) override
	{
		DataBatchKey key{};
		std::memcpy(&key, &token, sizeof(key));
		const uint32_t indexToRemove = FindIndex(key);
		if (indexToRemove == InvalidIndex) return;

		const size_t lastIndex = Items.size() - 1;

		if (indexToRemove != lastIndex)
		{
			// Swap with last for O(1) removal
			Items[indexToRemove] = std::move(Items[lastIndex]);
			uint32_t lastKey = IndexToKey[lastIndex];
			IndexToKey[indexToRemove] = lastKey;
			SetKeyIndex(lastKey, indexToRemove);
		}

		Items.pop_back();
		IndexToKey.pop_back();
		ClearKeyIndex(key.Value);
		bIsDirty = true;
		++VersionCounter;
	}

private:
	static constexpr uint32_t InvalidIndex = UINT32_MAX;

	void EnsureKeyIndexCapacity(uint32_t key)
	{
		const size_t requiredSize = static_cast<size_t>(key) + 1;
		if (KeyToIndex.size() < requiredSize)
			KeyToIndex.resize(requiredSize, InvalidIndex);
	}

	uint32_t FindIndex(DataBatchKey key) const
	{
		if (key.Value == 0 || key.Value >= KeyToIndex.size())
			return InvalidIndex;

		const uint32_t index = KeyToIndex[key.Value];
		if (index == InvalidIndex || static_cast<size_t>(index) >= Items.size())
			return InvalidIndex;

		return index;
	}

	void SetKeyIndex(uint32_t key, size_t index)
	{
		assert(index < static_cast<size_t>(InvalidIndex)
			&& "DataBatch dense index exceeds uint32_t storage.");
		EnsureKeyIndexCapacity(key);
		KeyToIndex[key] = static_cast<uint32_t>(index);
	}

	void ClearKeyIndex(uint32_t key)
	{
		if (key < KeyToIndex.size())
			KeyToIndex[key] = InvalidIndex;
	}

	void ClearItemsForRemoval()
	{
		Items.clear();
		IndexToKey.clear();
		KeyToIndex.clear();
		bIsDirty = true;
		++VersionCounter;
	}

	void RollbackEmplace(size_t oldSize, uint32_t oldNextKey)
	{
		for (size_t i = oldSize; i < IndexToKey.size(); ++i)
		{
			ClearKeyIndex(IndexToKey[i]);
		}

		Items.erase(Items.begin() + oldSize, Items.end());
		IndexToKey.erase(IndexToKey.begin() + oldSize, IndexToKey.end());
		NextKey = oldNextKey;
	}

	bool BlockCoversAllItems(DataBatchBlock block) const
	{
		for (uint32_t key : IndexToKey)
		{
			if (!block.Contains(DataBatchKey{ key }))
				return false;
		}
		return true;
	}

	bool RemoveKeySpan(std::span<const DataBatchKey> keys)
	{
		if (keys.empty() || Items.empty())
			return false;

		std::vector<size_t> indicesToRemove;
		indicesToRemove.reserve(keys.size());

		for (DataBatchKey key : keys)
		{
			if (key.Value == 0)
				continue;

			const uint32_t index = FindIndex(key);
			if (index != InvalidIndex)
				indicesToRemove.push_back(index);
		}

		return RemoveIndices(std::move(indicesToRemove));
	}

	bool RemoveKeyRange(uint32_t firstKey, size_t count)
	{
		std::vector<size_t> indicesToRemove;
		indicesToRemove.reserve(count);

		for (size_t i = 0; i < count; ++i)
		{
			const uint32_t key = firstKey + static_cast<uint32_t>(i);
			const uint32_t index = FindIndex(DataBatchKey{ key });
			if (index != InvalidIndex)
				indicesToRemove.push_back(index);
		}

		return RemoveIndices(std::move(indicesToRemove));
	}

	bool RemoveIndices(std::vector<size_t> indicesToRemove)
	{
		if (indicesToRemove.empty())
			return false;

		std::sort(indicesToRemove.begin(), indicesToRemove.end());
		indicesToRemove.erase(
			std::unique(indicesToRemove.begin(), indicesToRemove.end()),
			indicesToRemove.end());

		if (indicesToRemove.size() == Items.size())
		{
			ClearItemsForRemoval();
			return true;
		}

		std::vector<uint8_t> shouldRemove(Items.size(), uint8_t{ 0 });
		for (size_t index : indicesToRemove)
		{
			if (index < shouldRemove.size())
			{
				shouldRemove[index] = uint8_t{ 1 };
				ClearKeyIndex(IndexToKey[index]);
			}
		}

		size_t writeIndex = 0;
		for (size_t readIndex = 0; readIndex < Items.size(); ++readIndex)
		{
			if (shouldRemove[readIndex])
				continue;

			if (writeIndex != readIndex)
			{
				Items[writeIndex] = std::move(Items[readIndex]);
				IndexToKey[writeIndex] = IndexToKey[readIndex];
			}

			SetKeyIndex(IndexToKey[writeIndex], writeIndex);
			++writeIndex;
		}

		Items.erase(Items.begin() + writeIndex, Items.end());
		IndexToKey.erase(IndexToKey.begin() + writeIndex, IndexToKey.end());

		bIsDirty = true;
		++VersionCounter;
		return true;
	}

	std::vector<T> Items;                          // Dense, contiguous data
	std::vector<uint32_t> IndexToKey;              // Dense index → stable key
	std::vector<uint32_t> KeyToIndex;              // Stable key → dense index, or InvalidIndex
	uint32_t NextKey = 1;                          // Start at 1: key 0 == DataBatchKey{} == "invalid"
	bool bIsDirty = false;
	uint64_t VersionCounter = 0;
};
