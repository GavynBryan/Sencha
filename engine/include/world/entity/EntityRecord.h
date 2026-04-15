#pragma once

#include <core/batch/DataBatchKey.h>

//=============================================================================
// EntityRecord
//
// Per-entity metadata stored inside EntityRegistry. Holds the entity's
// transform key (for hierarchy lookup) and a type-erased destroy callback
// so the registry can tear down any entity type without knowing its concrete
// type.
//
// Owner / OwnerSlot / OnDestroy form a triad:
//   Owner     — opaque pointer to the EntityBatch<T> that owns this entity
//   OwnerSlot — the raw DataBatchKey.Value the batch uses to address the item
//   OnDestroy — called by EntityRegistry::Destroy() to remove the entity from
//               its owning batch; must NOT call EntityRegistry::Unregister —
//               the registry handles that after the callback returns
//=============================================================================
struct EntityRecord
{
	DataBatchKey TransformKey;
	void*        Owner     = nullptr;
	uint32_t     OwnerSlot = 0;
	void (*OnDestroy)(void* owner, uint32_t slot) = nullptr;
};
