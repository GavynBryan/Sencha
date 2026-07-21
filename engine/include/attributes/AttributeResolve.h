#pragma once

class StoragePartitionSet;
class World;

void ResolveAttributes(World& world);
void ResetAttributesToBase(World& world);
void ClampAttributes(World& world);

// Runtime scheduled variants. They visit only chunks owned by the phase's
// active partitions; dormant zones do no attribute work.
void ResolveAttributes(World& world, const StoragePartitionSet& partitions);
void ResetAttributesToBase(World& world, const StoragePartitionSet& partitions);
void ClampAttributes(World& world, const StoragePartitionSet& partitions);
