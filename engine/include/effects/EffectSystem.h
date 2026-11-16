#pragma once

#include <ecs/EntityId.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectId.h>
#include <effects/EffectSource.h>
#include <time/FrameClock.h>

#include <attributes/AttributeId.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class StoragePartitionSet;
class World;

// Overrides of one attribute that met at the top precedence on one target, each
// pair once: the fold kept the later in its canonical order (`Kept`). A World
// resource, made by the first tie.
struct EffectOverrideTie
{
    AttributeId Attribute;
    EffectId Kept;
    EffectId Passed;

    friend bool operator==(const EffectOverrideTie&, const EffectOverrideTie&) = default;
};

struct EffectOverrideTies
{
    std::vector<EffectOverrideTie> Seen;
};

// The effect entity a lasting effect spawns; none for an instant one. Its authored
// seconds become whole ticks of `now`'s tick length, and `scale` multiplies every
// modifier's magnitude, whatever its op. `source` is recorded with what an
// application changes, in the World's EffectApplicationLog when it has one.
EntityId ApplyEffect(World& world, EntityId target, EffectId effect, const FixedSimTime& now,
                     LeaseOwner owner = {}, float scale = 1.0f, const EffectSource& source = {});

// Ends one lasting effect, revoking the tags it granted. Not inside a query.
bool RemoveEffect(World& world, EntityId effect);
// Ends every lasting effect `owner` holds. Not inside a query.
std::size_t RemoveEffectsOwnedBy(World& world, const LeaseOwner& owner);

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
