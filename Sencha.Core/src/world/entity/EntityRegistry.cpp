#include <world/entity/EntityRegistry.h>

EntityKey EntityRegistry::Register(const EntityRecord& record)
{
	assert(record.Entity.IsValid()   && "Entities must have a valid handle.");
	assert(record.Owner != nullptr     && "EntityRecord Owner must not be null.");
	assert(record.OnDestroy != nullptr && "EntityRecord OnDestroy must not be null.");

	DataBatchKey batchKey = Records.EmplaceUnowned(record);
	EntityToRecord[record.Entity.Id] = batchKey.Value;
	return EntityKey{ batchKey };
}

void EntityRegistry::Unregister(EntityKey key)
{
	const EntityRecord* record = Records.TryGet(key.Value);
	if (!record) return;

	EntityToRecord.erase(record->Entity.Id);
	Records.RemoveKey(key.Value);
}

void EntityRegistry::Destroy(EntityKey key)
{
	const EntityRecord* record = Records.TryGet(key.Value);
	if (!record) return;

	// Capture before the record is invalidated by RemoveKey.
	auto onDestroy    = record->OnDestroy;
	auto owner        = record->Owner;
	auto ownerSlot    = record->OwnerSlot;
	auto entity       = record->Entity;

	EntityToRecord.erase(entity.Id);
	Records.RemoveKey(key.Value);

	if (onDestroy)
		onDestroy(owner, ownerSlot);
}

void EntityRegistry::DestroySubtree(EntityKey root, const TransformHierarchyService& hierarchy)
{
	const EntityRecord* rootRecord = Records.TryGet(root.Value);
	if (!rootRecord) return;

	std::vector<EntityHandle> postOrder;
	CollectPostOrder(hierarchy, rootRecord->Entity, postOrder);

	for (EntityHandle entity : postOrder)
	{
		EntityKey ek = FindByEntity(entity);
		if (ek)
			Destroy(ek);
	}
}

const EntityRecord* EntityRegistry::Find(EntityKey key) const
{
	return Records.TryGet(key.Value);
}

EntityKey EntityRegistry::FindByEntity(EntityHandle entity) const
{
	auto it = EntityToRecord.find(entity.Id);
	if (it == EntityToRecord.end()) return EntityKey{};
	return EntityKey{ DataBatchKey{ it->second } };
}

bool EntityRegistry::IsRegistered(EntityKey key) const
{
	return Records.Contains(key.Value);
}

size_t EntityRegistry::Count() const
{
	return Records.Count();
}

void EntityRegistry::CollectPostOrder(
	const TransformHierarchyService& hierarchy,
	EntityHandle entity,
	std::vector<EntityHandle>& out)
{
	for (EntityHandle child : hierarchy.GetChildren(entity))
		CollectPostOrder(hierarchy, child, out);
	out.push_back(entity);
}
