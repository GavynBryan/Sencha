#pragma once
#include <raii/LifetimeHandle.h>

//=============================================================================
// RefBatchHandle<T>
//
// Typed alias for LifetimeHandle<T, T*>, used with RefBatch<T>.
//
// Construction registers a pointer into a RefBatch; destruction removes it.
// GetToken() returns T*.
//
// Usage:
//   RefBatchHandle<IRenderable> handle(&renderableBatch, &myRenderable);
//   IRenderable* ptr = handle.GetToken();   // typed access
//=============================================================================
template<typename T>
using RefBatchHandle = LifetimeHandle<T, T*>;
