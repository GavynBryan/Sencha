#pragma once

#include <abilities/AbilityId.h>
#include <ecs/EntityId.h>

#include <cstdint>

class StoragePartitionSet;
class World;

// `tick` is the fixed tick the activation happens on, which names the animation it
// asks for (docs/gameplay/abilitykit.md, "Animation").
bool TryActivateAbility(World& world, EntityId actor, AbilityId ability, std::uint64_t tick);

// Cancels the Held animation of every activation that has ended since the last pass.
void ReleaseEndedAbilityAnimations(World& world, std::uint64_t tick);

// Unfiltered form for tests/tools. The scheduled form drains only intents whose
// actors belong to active logic partitions; dormant actors retain their intents
// for a later logic tick rather than activating while asleep or losing input.
// Both release ended activations' animation first.
void ProcessAbilityActivations(World& world, std::uint64_t tick);
void ProcessAbilityActivations(
    World& world,
    const StoragePartitionSet& partitions,
    std::uint64_t tick);
