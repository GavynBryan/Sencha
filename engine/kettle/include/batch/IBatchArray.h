#pragma once
#include <algorithm>
#include <cassert>
#include <span>
#include <unordered_map>
#include <vector>

//=============================================================================
// IBatchArray
// 
// Type-erased interface for batch arrays. Enables RAII handles to manage
// registration without knowing the concrete BatchArray<T> type.
// 
// Implementations that want to be services should also inherit from IService.
//=============================================================================
class IBatchArray
{
public:
	virtual ~IBatchArray() = default;
	virtual void Add(void* item) = 0;
	virtual void Remove(void* item) = 0;
	
	// Called when a registered item's state changes in a way that
	// may require the array to update (e.g., re-sort). Default no-op.
	virtual void MarkDirty() {}
};
