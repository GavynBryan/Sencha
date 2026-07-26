# ECS storage partitions

Status: Phase 1 substrate for the unified runtime world.

A `StoragePartitionId` is a dense runtime storage key inside one ECS `World`.
The ECS does not assign gameplay or streaming meaning to it. Runtime world
partition will later map resident zones to storage partitions; editor and test
worlds may use the same mechanism for other disjoint storage lanes.

## Storage contract

- Every archetype chunk belongs to exactly one storage partition.
- Rows from different partitions never share a chunk.
- Partition identity is structural metadata, not an ECS component.
- Existing unqualified `World` creation APIs use partition zero.
- Adding or removing components preserves the entity's partition.
- Command-buffer structural changes preserve the entity's partition.
- Queries still traverse every non-empty chunk in Phase 1. Domain-specific
  partition filtering is a later phase and must occur once per chunk, never once
  per entity.

The archetype retains a flat chunk array so existing cached queries and chunk
iteration do not gain another indirection. A per-archetype lookup caches the most
recent writable chunk for each partition. Structural operations pay the partition
lookup; ordinary row iteration does not.

## Entity migration

`World::MoveEntityToPartition` preserves the generational `EntityId` and component
signature, copies the row into a destination-partition chunk of the same archetype,
then swap-removes the source row.

A successful move:

- increments the global structural version once;
- increments both source and destination partition structural versions once;
- marks all destination columns changed for the current frame;
- appends one `EntityPartitionMove` record;
- leaves source swap-move change semantics unchanged.

Backends consume the migration journal to update their own secondary zone indices.
The ECS reports the structural fact and remains unaware of physics, audio,
navigation, or rendering object families.

Moving to the current partition is a no-op and emits no record.

## Partition destruction

`World::DestroyPartition` destroys every live entity owned by one partition using
the ordinary entity-destruction path. Component `OnRemove` hooks therefore run in
the same deterministic order and while world resources remain alive.

The initial implementation gathers generational entity IDs before destruction so
swap-removes cannot invalidate traversal. A later budgeted zone teardown layer may
spread calls across drain points without changing this ECS contract.

## Structural versions

`World::StructuralVersion()` remains the world-wide invalidation epoch.
`World::StructuralVersion(partition)` allows backend record families to reconcile
only the partition whose rows changed.

`StoragePartitionId` is 16-bit because it is a dense resident-runtime slot, not an
authored `ZoneId`. This bounds partition-indexed tables while allowing far more
simultaneously resident partitions than the product requires.

## Not implemented by this phase

This substrate does not yet:

- replace runtime zone registries;
- filter queries by active partitions;
- map `ZoneId` to `StoragePartitionId`;
- bulk-import detached zone packages;
- consolidate backend scenes;
- remove `RegistryId` or `EntityRef`.

Those changes build on this storage contract in later unified-world phases.
