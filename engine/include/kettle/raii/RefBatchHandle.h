#pragma once
#include <kettle/raii/LifetimeHandle.h>

//=============================================================================
// RefBatchHandle<T>
//
// Typed alias for LifetimeHandle<T*>, used with RefBatch<T>.
//
// Construction registers a pointer into a RefBatch; destruction removes it.
// GetToken() returns T* — no void* ever visible to the caller.
//
// Usage:
//   RefBatchHandle<IRenderable> handle(&renderableBatch, &myRenderable);
//   IRenderable* ptr = handle.GetToken();   // typed access
//=============================================================================
template<typename T>
using RefBatchHandle = LifetimeHandle<T*>;
