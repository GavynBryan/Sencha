#pragma once
#include <core/batch/DataBatchKey.h>
#include <core/raii/DataBatchHandle.h>
#include <core/service/IService.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

//=============================================================================
// DataBatch<T>
//
// Templated service that owns a contiguous array of T values. Items receive a
// stable DataBatchKey so swap-and-pop removal does not invalidate other handles.
// The key packs a recyclable index plus a generation, so stale keys stop
// resolving after their slot is removed and later reused.
//=============================================================================
template<typename T>
class DataBatch : public ILifetimeOwner, public IService
{
	static_assert(std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>,
		"DataBatch<T> requires T to be at least move-constructible.");

	static constexpr uint32_t InvalidIndex = UINT32_MAX;

	struct KeySlot
	{
		uint32_t DenseIndex = InvalidIndex;
		uint32_t Generation = 0;
	};

	struct AllocatedKey
	{
		DataBatchKey Key;
		bool FromFreeList = false;
	};

public:
	// -- Emplacement (the primary way to add data) --------------------------

	template<typename... Args>
	DataBatchHandle<T> Emplace(Args&&... args)
	{
		DataBatchKey key = EmplaceUnowned(std::forward<Args>(args)...);

		// Use NoAttach: the item is already in the batch.
		return DataBatchHandle<T>(
			this, key, DataBatchHandle<T>::NoAttach);
	}

	template<typename... Args>
	DataBatchKey EmplaceUnowned(Args&&... args)
	{
		const AllocatedKey allocated = AllocateKey();
		try
		{
			InsertAllocatedKey(allocated.Key, std::forward<Args>(args)...);
		}
		catch (...)
		{
			RollbackAllocatedKey(allocated);
			throw;
		}

		bIsDirty = true;
		++VersionCounter;
		return allocated.Key;
	}

	template<typename... Args>
	void EmplaceAtKey(DataBatchKey key, Args&&... args)
	{
		ValidateExplicitKey(key);

		const uint32_t keyIndex = key.Index();
		EnsureKeySlotCapacity(keyIndex);
		KeySlot& slot = KeySlots[keyIndex];

		if (slot.DenseIndex != InvalidIndex || slot.Generation != key.Generation())
			throw std::invalid_argument("DataBatch explicit key is not available.");

		const bool removedFromFreeList = RemoveFreeKeyIndex(keyIndex);
		try
		{
			InsertAllocatedKey(key, std::forward<Args>(args)...);
		}
		catch (...)
		{
			if (removedFromFreeList)
				FreeKeyIndices.push_back(keyIndex);
			throw;
		}

		if (NextKeyIndex <= keyIndex)
			NextKeyIndex = keyIndex + 1;

		bIsDirty = true;
		++VersionCounter;
	}

	template<typename Factory>
	DataBatchBlock EmplaceBlock(size_t count, Factory&& factory)
	{
		if (count == 0)
			return DataBatchBlock{};

		if (NextKeyIndex > DataBatchKey::MaxIndex
			|| count > static_cast<size_t>(DataBatchKey::MaxIndex - NextKeyIndex + 1))
			throw std::length_error("DataBatch key range exhausted.");

		const size_t oldSize = Items.size();
		const uint32_t oldNextKeyIndex = NextKeyIndex;
		const uint32_t firstKeyIndex = NextKeyIndex;
		const uint32_t lastKeyIndex = firstKeyIndex + static_cast<uint32_t>(count - 1);
		Reserve(Items.size() + count);
		EnsureKeySlotCapacity(lastKeyIndex);

		try
		{
			for (size_t i = 0; i < count; ++i)
			{
				decltype(auto) item = factory(i);
				Items.emplace_back(std::forward<decltype(item)>(item));
			}

			for (size_t i = 0; i < count; ++i)
			{
				const uint32_t keyIndex = firstKeyIndex + static_cast<uint32_t>(i);
				const DataBatchKey key = DataBatchKey::FromParts(
					keyIndex,
					KeySlots[keyIndex].Generation);
				IndexToKey.push_back(key.Value);
			}

			for (size_t i = 0; i < count; ++i)
			{
				SetKeyIndex(DataBatchKey{ IndexToKey[oldSize + i] }, oldSize + i);
			}
		}
		catch (...)
		{
			RollbackEmplace(oldSize, oldNextKeyIndex);
			throw;
		}

		NextKeyIndex = firstKeyIndex + static_cast<uint32_t>(count);
		bIsDirty = true;
		++VersionCounter;

		return DataBatchBlock{
			DataBatchKey::FromParts(firstKeyIndex, KeySlots[firstKeyIndex].Generation).Value,
			count
		};
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

	void RemoveKey(DataBatchKey key)
	{
		RemoveSingleKey(key);
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
	uint32_t IndexOf(DataBatchKey key) const
	{
		return FindIndex(key);
	}

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
			const size_t targetSlotCapacity =
				static_cast<size_t>(NextKeyIndex) + additionalKeyCapacity;
			if (targetSlotCapacity <= static_cast<size_t>(DataBatchKey::MaxIndex) + 1)
				KeySlots.reserve(targetSlotCapacity);
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

		std::vector<size_t> perm(Items.size());
		std::iota(perm.begin(), perm.end(), size_t{0});

		std::sort(perm.begin(), perm.end(), [&](size_t a, size_t b)
		{
			return comp(Items[a], Items[b]);
		});

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

		for (size_t i = 0; i < Items.size(); ++i)
		{
			SetKeyIndex(DataBatchKey{ IndexToKey[i] }, i);
		}

		bIsDirty = false;
		++VersionCounter;
	}

	// -- Housekeeping -------------------------------------------------------

	void Clear()
	{
		for (uint32_t rawKey : IndexToKey)
			DeactivateKey(DataBatchKey{ rawKey });

		Items.clear();
		IndexToKey.clear();
		bIsDirty = false;
		++VersionCounter;
	}

	auto begin() { return Items.begin(); }
	auto end() { return Items.end(); }
	auto begin() const { return Items.begin(); }
	auto end() const { return Items.end(); }

protected:
	// -- ILifetimeOwner -----------------------------------------------------

	void Attach(uint64_t /*token*/) override {}

	void Detach(uint64_t token) override
	{
		DataBatchKey key{};
		std::memcpy(&key, &token, sizeof(key));
		RemoveSingleKey(key);
	}

private:
	void ValidateExplicitKey(DataBatchKey key) const
	{
		if (key.Value == 0 || key.Index() == 0)
			throw std::invalid_argument("DataBatch explicit key cannot be null.");
	}

	void RemoveSingleKey(DataBatchKey key)
	{
		const uint32_t indexToRemove = FindIndex(key);
		if (indexToRemove == InvalidIndex) return;

		const size_t lastIndex = Items.size() - 1;

		if (indexToRemove != lastIndex)
		{
			Items[indexToRemove] = std::move(Items[lastIndex]);
			uint32_t lastKey = IndexToKey[lastIndex];
			IndexToKey[indexToRemove] = lastKey;
			SetKeyIndex(DataBatchKey{ lastKey }, indexToRemove);
		}

		Items.pop_back();
		IndexToKey.pop_back();
		DeactivateKey(key);
		bIsDirty = true;
		++VersionCounter;
	}

	void EnsureKeySlotCapacity(uint32_t keyIndex)
	{
		const size_t requiredSize = static_cast<size_t>(keyIndex) + 1;
		if (KeySlots.size() < requiredSize)
			KeySlots.resize(requiredSize);
	}

	AllocatedKey AllocateKey()
	{
		while (!FreeKeyIndices.empty())
		{
			const uint32_t keyIndex = FreeKeyIndices.back();
			FreeKeyIndices.pop_back();
			if (keyIndex < KeySlots.size()
				&& KeySlots[keyIndex].DenseIndex == InvalidIndex
				&& KeySlots[keyIndex].Generation <= DataBatchKey::MaxGeneration)
			{
				return {
					DataBatchKey::FromParts(keyIndex, KeySlots[keyIndex].Generation),
					true
				};
			}
		}

		if (NextKeyIndex > DataBatchKey::MaxIndex)
			throw std::length_error("DataBatch key range exhausted.");

		const uint32_t keyIndex = NextKeyIndex++;
		EnsureKeySlotCapacity(keyIndex);
		return {
			DataBatchKey::FromParts(keyIndex, KeySlots[keyIndex].Generation),
			false
		};
	}

	void RollbackAllocatedKey(AllocatedKey allocated)
	{
		const uint32_t keyIndex = allocated.Key.Index();
		if (allocated.FromFreeList)
		{
			FreeKeyIndices.push_back(keyIndex);
		}
		else if (NextKeyIndex == keyIndex + 1)
		{
			--NextKeyIndex;
		}
	}

	template<typename... Args>
	void InsertAllocatedKey(DataBatchKey key, Args&&... args)
	{
		const size_t oldSize = Items.size();
		try
		{
			Items.emplace_back(std::forward<Args>(args)...);
			IndexToKey.push_back(key.Value);
			SetKeyIndex(key, Items.size() - 1);
		}
		catch (...)
		{
			if (key.Index() < KeySlots.size()
				&& KeySlots[key.Index()].DenseIndex == oldSize)
			{
				KeySlots[key.Index()].DenseIndex = InvalidIndex;
			}

			if (Items.size() > oldSize)
				Items.erase(Items.begin() + oldSize, Items.end());
			if (IndexToKey.size() > oldSize)
				IndexToKey.erase(IndexToKey.begin() + oldSize, IndexToKey.end());
			throw;
		}
	}

	uint32_t FindIndex(DataBatchKey key) const
	{
		if (key.Value == 0 || key.Index() >= KeySlots.size())
			return InvalidIndex;

		const KeySlot& slot = KeySlots[key.Index()];
		if (slot.Generation != key.Generation())
			return InvalidIndex;

		const uint32_t index = slot.DenseIndex;
		if (index == InvalidIndex || static_cast<size_t>(index) >= Items.size())
			return InvalidIndex;

		if (IndexToKey[index] != key.Value)
			return InvalidIndex;

		return index;
	}

	void SetKeyIndex(DataBatchKey key, size_t index)
	{
		assert(index < static_cast<size_t>(InvalidIndex)
			&& "DataBatch dense index exceeds uint32_t storage.");
		EnsureKeySlotCapacity(key.Index());
		KeySlot& slot = KeySlots[key.Index()];
		slot.DenseIndex = static_cast<uint32_t>(index);
		slot.Generation = key.Generation();
	}

	void DeactivateKey(DataBatchKey key)
	{
		if (key.Value == 0 || key.Index() >= KeySlots.size())
			return;

		KeySlot& slot = KeySlots[key.Index()];
		if (slot.Generation != key.Generation())
			return;

		slot.DenseIndex = InvalidIndex;
		if (slot.Generation < DataBatchKey::MaxGeneration)
		{
			++slot.Generation;
			FreeKeyIndices.push_back(key.Index());
		}
	}

	bool RemoveFreeKeyIndex(uint32_t keyIndex)
	{
		auto it = std::find(FreeKeyIndices.begin(), FreeKeyIndices.end(), keyIndex);
		if (it == FreeKeyIndices.end())
			return false;

		FreeKeyIndices.erase(it);
		return true;
	}

	void ClearItemsForRemoval()
	{
		for (uint32_t rawKey : IndexToKey)
			DeactivateKey(DataBatchKey{ rawKey });

		Items.clear();
		IndexToKey.clear();
		bIsDirty = true;
		++VersionCounter;
	}

	void RollbackEmplace(size_t oldSize, uint32_t oldNextKeyIndex)
	{
		for (size_t i = oldSize; i < IndexToKey.size(); ++i)
		{
			DataBatchKey key{ IndexToKey[i] };
			if (key.Index() < KeySlots.size()
				&& KeySlots[key.Index()].Generation == key.Generation())
			{
				KeySlots[key.Index()].DenseIndex = InvalidIndex;
			}
		}

		Items.erase(Items.begin() + oldSize, Items.end());
		IndexToKey.erase(IndexToKey.begin() + oldSize, IndexToKey.end());
		NextKeyIndex = oldNextKeyIndex;
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
		const DataBatchBlock block{ firstKey, count };

		for (size_t i = 0; i < count; ++i)
		{
			const uint32_t index = FindIndex(block.KeyAt(i));
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
				DeactivateKey(DataBatchKey{ IndexToKey[index] });
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

			SetKeyIndex(DataBatchKey{ IndexToKey[writeIndex] }, writeIndex);
			++writeIndex;
		}

		Items.erase(Items.begin() + writeIndex, Items.end());
		IndexToKey.erase(IndexToKey.begin() + writeIndex, IndexToKey.end());

		bIsDirty = true;
		++VersionCounter;
		return true;
	}

	std::vector<T> Items;
	std::vector<uint32_t> IndexToKey;
	std::vector<KeySlot> KeySlots;
	std::vector<uint32_t> FreeKeyIndices;
	uint32_t NextKeyIndex = 1;
	bool bIsDirty = false;
	uint64_t VersionCounter = 0;
};
