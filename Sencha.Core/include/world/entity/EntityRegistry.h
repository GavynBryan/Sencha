#pragma once

#include <core/batch/DataBatch.h>
#include <core/batch/DataBatchKey.h>
#include <unordered_map>
#include <vector>
#include <world/entity/EntityKey.h>
#include <world/entity/EntityRecord.h>
#include <transform/TransformHierarchyService.h>

//=============================================================================
// EntityRegistry
//
// Central registry mapping EntityKey -> EntityRecord. Lives inside
// World<TTransform> — one registry per world dimension (2D or 3D).
//
// Responsibilities:
//   - Issue stable EntityKeys backed by DataBatch generation tracking
//   - Maintain the single transform-key -> entity-key reverse map
//   - Invoke type-erased OnDestroy callbacks for Destroy() and DestroySubtree()
//
// The registry does NOT own entity structs. Those are owned by the
// application's EntityBatch<T>. The registry only owns the EntityRecord
// metadata and the reverse lookup.
//
// Destroy ordering: the registry removes the EntityRecord and reverse-map
// entry BEFORE invoking OnDestroy. This ensures a clean registry state
// during any RAII teardown (TransformHierarchyRegistration, etc.) triggered
// by the callback.
//=============================================================================
class EntityRegistry
{
public:
	EntityRegistry() = default;

	EntityRegistry(const EntityRegistry&) = delete;
	EntityRegistry& operator=(const EntityRegistry&) = delete;
	EntityRegistry(EntityRegistry&&) = delete;
	EntityRegistry& operator=(EntityRegistry&&) = delete;

	// -- Registration ----------------------------------------------------------

	// Register an entity and return its stable key.
	// record.TransformKey must be non-null — all entities require a transform.
	EntityKey Register(const EntityRecord& record);

	// Remove an entity's record without invoking its destroy callback.
	// Prefer Destroy() for normal teardown.
	void Unregister(EntityKey key);

	// -- Destruction -----------------------------------------------------------

	// Remove the entity's record and invoke its OnDestroy callback.
	// The record is removed from the registry before OnDestroy fires so that
	// RAII teardown inside the callback (e.g. TransformHierarchyRegistration)
	// observes a clean registry state.
	void Destroy(EntityKey key);

	// Destroy an entity and all of its transform-hierarchy descendants,
	// leaves first. The full subtree is collected before any destruction
	// begins so that hierarchy modifications during teardown do not affect
	// the traversal order.
	void DestroySubtree(EntityKey root, const TransformHierarchyService& hierarchy);

	// -- Queries ---------------------------------------------------------------

	const EntityRecord* Find(EntityKey key) const;
	EntityKey           FindByTransform(DataBatchKey transformKey) const;
	bool                IsRegistered(EntityKey key) const;
	size_t              Count() const;

private:
	static void CollectPostOrder(
		const TransformHierarchyService& hierarchy,
		DataBatchKey key,
		std::vector<DataBatchKey>& out);

	DataBatch<EntityRecord>                Records;
	std::unordered_map<uint32_t, uint32_t> TransformToEntity;
};
