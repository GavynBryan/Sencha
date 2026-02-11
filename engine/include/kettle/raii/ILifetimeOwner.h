#pragma once

//=============================================================================
// ILifetimeOwner
//
// Type-erased interface for any object that manages the lifetime of
// resources via LifetimeHandle. The handle calls Attach on construction
// and Detach on destruction, enabling RAII ownership across unrelated
// subsystems (batches, pools, registries, etc.).
//
// The void* parameter is the type-erasure boundary — callers never see
// it because LifetimeHandle<TokenT> performs the cast internally:
//   - For pointer tokens (T*), the pointer itself is passed.
//   - For value tokens (e.g., DataBatchKey), a pointer to the stored
//     token is passed.
//=============================================================================
class ILifetimeOwner
{
public:
	virtual ~ILifetimeOwner() = default;

	// Called by LifetimeHandle on construction.
	virtual void Attach(void* token) = 0;

	// Called by LifetimeHandle on destruction / reset.
	virtual void Detach(void* token) = 0;
};
