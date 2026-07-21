#pragma once

#include <abilities/AbilityId.h>
#include <ecs/EntityId.h>

class StoragePartitionSet;
class World;

bool TryActivateAbility(World& world, EntityId actor, AbilityId ability);

// Unfiltered form for tests/tools. The scheduled form drains only intents whose
// actors belong to active logic partitions; dormant actors retain their intents
// for a later logic tick rather than activating while asleep or losing input.
void ProcessAbilityActivations(World& world);
void ProcessAbilityActivations(
    World& world,
    const StoragePartitionSet& partitions);
