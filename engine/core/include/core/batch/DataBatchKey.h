#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

//=============================================================================
// DataBatchKey
//
// Strongly-typed key that identifies an item inside a DataBatch.
// Default-constructed keys have Value == 0, which DataBatchHandle treats
// as the null sentinel.
//=============================================================================
struct DataBatchKey
{
	static constexpr uint32_t IndexBits = 20;
	static constexpr uint32_t GenerationBits = 12;
	static constexpr uint32_t IndexMask = (uint32_t{ 1 } << IndexBits) - 1u;
	static constexpr uint32_t GenerationMask = (uint32_t{ 1 } << GenerationBits) - 1u;
	static constexpr uint32_t MaxIndex = IndexMask;
	static constexpr uint32_t MaxGeneration = GenerationMask;
	static constexpr uint32_t GenerationShift = IndexBits;

	uint32_t Value = 0;

	constexpr DataBatchKey() = default;
	constexpr explicit DataBatchKey(uint32_t value) : Value(value) {}

	static constexpr DataBatchKey FromParts(uint32_t index, uint32_t generation)
	{
		assert(index <= MaxIndex && "DataBatchKey index exceeds packed storage.");
		assert(generation <= MaxGeneration && "DataBatchKey generation exceeds packed storage.");
		return DataBatchKey{
			(index & IndexMask) | ((generation & GenerationMask) << GenerationShift)
		};
	}

	constexpr uint32_t Index() const { return Value & IndexMask; }
	constexpr uint32_t Generation() const { return (Value >> GenerationShift) & GenerationMask; }

	bool operator==(const DataBatchKey&) const = default;
};

//=============================================================================
// DataBatchBlock
//
// Lightweight metadata describing a contiguous key range allocated by one
// DataBatch::EmplaceBlock call. It does not own handles or keep items alive.
//=============================================================================
struct DataBatchBlock
{
	// First key in a contiguous key range returned by DataBatch::EmplaceBlock.
	uint32_t FirstKey = 0;

	// Number of keys in the block. A zero-count block is empty/invalid.
	size_t Count = 0;

	bool IsEmpty() const { return Count == 0; }

	// Convert a block-local index to its DataBatchKey.
	DataBatchKey KeyAt(size_t index) const
	{
		assert(index < Count && "DataBatchBlock key index out of range.");
		const DataBatchKey first{ FirstKey };
		const uint32_t keyIndex = first.Index() + static_cast<uint32_t>(index);
		assert(keyIndex <= DataBatchKey::MaxIndex && "DataBatchBlock key range exceeds index storage.");
		return DataBatchKey::FromParts(keyIndex, first.Generation());
	}

	// Check whether a key falls inside this block's contiguous key range.
	bool Contains(DataBatchKey key) const
	{
		const DataBatchKey first{ FirstKey };
		return key.Generation() == first.Generation()
			&& key.Index() >= first.Index()
			&& static_cast<size_t>(key.Index() - first.Index()) < Count;
	}
};
