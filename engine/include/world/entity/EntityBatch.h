#pragma once

#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <span>
#include <world/entity/EntityKey.h>
#include <world/entity/EntityRecord.h>
#include <world/entity/EntityRegistry.h>
#include <world/entity/IsEntity.h>

//=============================================================================
// EntityBatch<T>
//
// Typed entity container. Wraps DataBatch<T> and auto-wires registration with
// EntityRegistry on emplace. T must satisfy IsEntity — it must expose a
// TransformKey() const -> DataBatchKey method. The minimal conforming struct
// just forwards from its embedded TransformNode:
//
//   struct Goblin
//   {
//       TransformNode2d Node;
//       // ... state
//       DataBatchKey TransformKey() const { return Node.TransformKey(); }
//   };
//
//   EntityBatch<Goblin> Goblins{ world.Entities };
//   EntityKey ek = Goblins.Emplace(domain, Transform2f{});
//   world.DestroySubtree(ek);  // recursively destroys Goblin + all children
//
// EntityBatch<T> is non-copyable and non-movable because it stores `this` as
// the Owner pointer in each EntityRecord. Moving would silently dangle those
// pointers.
//
// Hot-path iteration should use GetItems() directly — it returns a contiguous
// span of T with no registry involvement. EntityKey -> T lookup via TryGet()
// is a cold-path operation (one registry Find + one batch TryGet).
//=============================================================================
template <IsEntity T>
class EntityBatch
{
public:
	explicit EntityBatch(EntityRegistry& registry)
		: Registry(registry)
	{}

	EntityBatch(const EntityBatch&) = delete;
	EntityBatch& operator=(const EntityBatch&) = delete;
	EntityBatch(EntityBatch&&) = delete;
	EntityBatch& operator=(EntityBatch&&) = delete;

	// -- Emplacement -----------------------------------------------------------

	template <typename... Args>
	EntityKey Emplace(Args&&... args)
	{
		DataBatchKey batchKey = Batch.EmplaceUnowned(std::forward<Args>(args)...);
		T* item = Batch.TryGet(batchKey);

		return Registry.Register({
			.TransformKey = item->TransformKey(),
			.Owner        = this,
			.OwnerSlot    = batchKey.Value,
			.OnDestroy    = &EntityBatch::OnDestroyItem,
		});
	}

	// -- Access ----------------------------------------------------------------

	// Look up an entity struct by entity key. Cold path — goes through the
	// registry to resolve the batch slot. Use GetItems() for bulk iteration.
	T* TryGet(EntityKey key)
	{
		const EntityRecord* record = Registry.Find(key);
		if (!record || record->Owner != this) return nullptr;
		return Batch.TryGet(DataBatchKey{ record->OwnerSlot });
	}

	const T* TryGet(EntityKey key) const
	{
		const EntityRecord* record = Registry.Find(key);
		if (!record || record->Owner != this) return nullptr;
		return Batch.TryGet(DataBatchKey{ record->OwnerSlot });
	}

	// Contiguous span of all live entities. Does not involve EntityRegistry.
	std::span<T>       GetItems()       { return Batch.GetItems(); }
	std::span<const T> GetItems() const { return Batch.GetItems(); }

	size_t Count()   const { return Batch.Count(); }
	bool   IsEmpty() const { return Batch.IsEmpty(); }

private:
	static void OnDestroyItem(void* self, uint32_t slot)
	{
		static_cast<EntityBatch<T>*>(self)->Batch.RemoveKey(DataBatchKey{ slot });
	}

	DataBatch<T>    Batch;
	EntityRegistry& Registry;
};
