#pragma once
#include <core/raii/LifetimeHandle.h>

//=============================================================================
// InstanceRegistryHandle<T>
//
// Typed alias for LifetimeHandle<T, T*>, used with InstanceRegistry<T>.
//
// Construction registers a pointer into the registry; destruction removes it.
// GetToken() returns T*.
//
// Usage:
//   InstanceRegistryHandle<IRenderable> handle(&renderableRegistry, &myRenderable);
//   IRenderable* ptr = handle.GetToken();   // typed access
//=============================================================================
template<typename T>
using InstanceRegistryHandle = LifetimeHandle<T, T*>;
