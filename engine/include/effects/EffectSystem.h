#pragma once

#include <ecs/EntityId.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectId.h>
#include <time/FrameClock.h>

#include <cstddef>
#include <cstdint>

class StoragePartitionSet;
class World;

// The effect entity a lasting effect spawns; none for an instant one. Its authored
// seconds become whole ticks of `now`'s tick length.
EntityId ApplyEffect(World& world, EntityId target, EffectId effect, const FixedSimTime& now,
                     EffectOwner owner = {});

// Ends one lasting effect, revoking the tags it granted. Not inside a query.
bool RemoveEffect(World& world, EntityId effect);
// Ends every lasting effect `owner` holds. Not inside a query.
std::size_t RemoveEffectsOwnedBy(World& world, const EffectOwner& owner);

// One fixed tick's lifetime pass: counts every active target's effects down a tick;
// a period or end that falls due is what the next tick sees.
void TickEffects(World& world);
void FoldActiveEffects(World& world);
void ResolveAttributesWithEffects(World& world);

// Scheduled runtime variants: only effects whose target is in an active
// partition advance or fold.
void TickEffects(
    World& world,
    const StoragePartitionSet& partitions);
void FoldActiveEffects(
    World& world,
    const StoragePartitionSet& partitions);
void ResolveAttributesWithEffects(
    World& world,
    const StoragePartitionSet& partitions);
