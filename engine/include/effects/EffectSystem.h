#pragma once

#include <ecs/EntityId.h>
#include <effects/EffectId.h>

class StoragePartitionSet;
class World;

void ApplyEffect(World& world, EntityId target, EffectId effect);

void TickEffects(World& world, float dt);
void FoldActiveEffects(World& world);
void ResolveAttributesWithEffects(World& world);

// Scheduled runtime variants. Active-effect entities are stored with their
// target zone and only active partition chunks advance or fold.
void TickEffects(
    World& world,
    float dt,
    const StoragePartitionSet& partitions);
void FoldActiveEffects(
    World& world,
    const StoragePartitionSet& partitions);
void ResolveAttributesWithEffects(
    World& world,
    const StoragePartitionSet& partitions);
