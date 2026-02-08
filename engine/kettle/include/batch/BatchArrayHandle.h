#pragma once
#include <batch/IBatchArray.h>

//=============================================================================
// BatchArrayHandle
// 
// RAII handle that automatically removes an item from a BatchArray
// when destroyed or reset. Objects can hold multiple handles for
// registration with multiple arrays.
//=============================================================================
class BatchArrayHandle
{
public:
	BatchArrayHandle() = default;
	
	// Adds the item and takes ownership of removal
	BatchArrayHandle(IBatchArray* array, void* item)
		: Array(array)
		, Item(item)
	{
		if (Array && Item)
		{
			Array->Add(Item);
		}
	}

	~BatchArrayHandle()
	{
		Reset();
	}

	// Move-only semantics
	BatchArrayHandle(BatchArrayHandle&& other) noexcept
		: Array(other.Array)
		, Item(other.Item)
	{
		other.Array = nullptr;
		other.Item = nullptr;
	}

	BatchArrayHandle& operator=(BatchArrayHandle&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			Array = other.Array;
			Item = other.Item;
			other.Array = nullptr;
			other.Item = nullptr;
		}
		return *this;
	}

	BatchArrayHandle(const BatchArrayHandle&) = delete;
	BatchArrayHandle& operator=(const BatchArrayHandle&) = delete;

	// Manually release the handle (removes from array)
	void Reset()
	{
		if (Array && Item)
		{
			Array->Remove(Item);
		}
		Array = nullptr;
		Item = nullptr;
	}

	// Check if handle is valid
	bool IsValid() const { return Array != nullptr && Item != nullptr; }
	explicit operator bool() const { return IsValid(); }

	// Access the array (for advanced use cases)
	IBatchArray* GetArray() const { return Array; }

private:
	IBatchArray* Array = nullptr;
	void* Item = nullptr;
};