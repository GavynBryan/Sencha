#pragma once

//=============================================================================
// IComponentStore
//
// Minimal type-erased base for entity-indexed component stores. Stores derive
// from this so ComponentRegistry can own them uniformly via unique_ptr without
// knowing their component types.
//
// No virtual data-access methods live here — callers cache a typed pointer
// (HealthStore*, SpriteStore*, …) at init time so hot-path access has zero
// overhead beyond a direct pointer dereference.
//=============================================================================
class IComponentStore
{
public:
    virtual ~IComponentStore() = default;
};
