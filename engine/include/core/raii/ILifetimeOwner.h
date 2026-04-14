#pragma once
#include <cstdint>

//=============================================================================
// ILifetimeOwner
//
// Type-erased interface for any object that manages the lifetime of
// resources via LifetimeHandle. The handle calls Attach on construction
// and Detach on destruction, enabling RAII ownership across unrelated
// subsystems (batches, pools, registries, etc.).
//
// The uint64_t token is an owner-interpreted opaque slot. InstanceRegistry
// decodes it as a pointer; DataBatch decodes it as a compact key value.
//=============================================================================
class ILifetimeOwner
{
public:
	virtual ~ILifetimeOwner() = default;

	// Called by LifetimeHandle on construction.
	virtual void Attach(uint64_t token) = 0;

	// Called by LifetimeHandle on destruction / reset.
	virtual void Detach(uint64_t token) = 0;
};
