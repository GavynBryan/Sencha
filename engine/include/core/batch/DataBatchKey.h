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
	uint32_t Value = 0;
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
		return DataBatchKey{ FirstKey + static_cast<uint32_t>(index) };
	}

	// Check whether a key falls inside this block's contiguous key range.
	bool Contains(DataBatchKey key) const
	{
		return key.Value >= FirstKey
			&& static_cast<size_t>(key.Value - FirstKey) < Count;
	}
};
