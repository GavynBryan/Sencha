#include <world/entity/EntityRegistry.h>

EntityKey EntityRegistry::Register(const EntityRecord& record)
{
	assert(record.TransformKey.Value != 0 && "Entities must have a transform.");
	assert(record.Owner != nullptr     && "EntityRecord Owner must not be null.");
	assert(record.OnDestroy != nullptr && "EntityRecord OnDestroy must not be null.");

	DataBatchKey batchKey = Records.EmplaceUnowned(record);
	TransformToEntity[record.TransformKey.Value] = batchKey.Value;
	return EntityKey{ batchKey };
}

void EntityRegistry::Unregister(EntityKey key)
{
	const EntityRecord* record = Records.TryGet(key.Value);
	if (!record) return;

	TransformToEntity.erase(record->TransformKey.Value);
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
	auto transformKey = record->TransformKey;

	TransformToEntity.erase(transformKey.Value);
	Records.RemoveKey(key.Value);

	if (onDestroy)
		onDestroy(owner, ownerSlot);
}

void EntityRegistry::DestroySubtree(EntityKey root, const TransformHierarchyService& hierarchy)
{
	const EntityRecord* rootRecord = Records.TryGet(root.Value);
	if (!rootRecord) return;

	std::vector<DataBatchKey> postOrder;
	CollectPostOrder(hierarchy, rootRecord->TransformKey, postOrder);

	for (DataBatchKey tkey : postOrder)
	{
		EntityKey ek = FindByTransform(tkey);
		if (ek)
			Destroy(ek);
	}
}

const EntityRecord* EntityRegistry::Find(EntityKey key) const
{
	return Records.TryGet(key.Value);
}

EntityKey EntityRegistry::FindByTransform(DataBatchKey transformKey) const
{
	auto it = TransformToEntity.find(transformKey.Value);
	if (it == TransformToEntity.end()) return EntityKey{};
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
	DataBatchKey key,
	std::vector<DataBatchKey>& out)
{
	for (uint32_t childVal : hierarchy.GetChildren(key))
		CollectPostOrder(hierarchy, DataBatchKey{ childVal }, out);
	out.push_back(key);
}
