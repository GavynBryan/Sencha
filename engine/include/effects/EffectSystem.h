#pragma once

#include <ecs/EntityId.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectId.h>
#include <effects/EffectSource.h>
#include <time/FrameClock.h>

#include <attributes/AttributeId.h>
#include <core/identity/Id.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class GameplayTagQuery;
class StoragePartitionSet;
class World;

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

struct EffectApplicationAdmission
{
    bool (*Admit)(World& world, EntityId target, EffectId effect) = nullptr;
    void (*Applied)(World& world, EntityId target, EffectId effect, EntityId instance) = nullptr;
};

// Whether a save keeps a lasting effect: only its applier knows whether it will
// apply it again when the world is rebuilt (docs/gameplay/saves.md, "Lasting effects").
enum class EffectKeeping : std::uint8_t
{
    // Kept when nothing owns it; not kept when something does.
    ByOwner,
    Kept,
    NotKept,
};

// What a save holds of one lasting effect: its time left, never an absolute tick.
struct KeptEffect
{
    EffectId Effect;
    std::uint32_t RemainingTicks = kEffectNeverEnds;
    std::uint32_t TicksToPeriod = 0;
    float Scale = 1.0f;
    // Its owner, when it had one a save could name; it owns the restored effect if resident then.
    PersistentEntityId Owner;
    std::uint64_t OwnerKey = 0;
};

// The effect entity a lasting effect spawns; none for an instant one, or one
// EffectApplicationAdmission refuses. Its authored
// seconds become whole ticks of `now`'s tick length, and `scale` multiplies every
// modifier's magnitude, whatever its op. `source` is recorded with what an
// application changes, in the World's EffectApplicationLog when it has one.
EntityId ApplyEffect(World& world, EntityId target, EffectId effect, const FixedSimTime& now,
                     LeaseOwner owner = {}, float scale = 1.0f, const EffectSource& source = {},
                     EffectKeeping keeping = EffectKeeping::ByOwner);

// Puts a kept effect back with its time left: what it grants and changes. It was
// admitted when it was applied, so no admission, no application hook and no log.
EntityId RestoreEffect(World& world, EntityId target, const KeptEffect& kept, const FixedSimTime& now);

// Ends one lasting effect, revoking the tags it granted. Not inside a query.
bool RemoveEffect(World& world, EntityId effect);
// Ends every lasting effect `owner` holds. Not inside a query.
std::size_t RemoveEffectsOwnedBy(World& world, const LeaseOwner& owner);
// Ends every lasting effect on `target` of that definition, or whose kinds match. Not inside a query.
std::size_t RemoveEffectsOn(World& world, EntityId target, EffectId effect);
std::size_t RemoveEffectsOn(World& world, EntityId target, const GameplayTagQuery& kinds);
void FindEffectsOwnedBy(const World& world, const LeaseOwner& owner, std::vector<EntityId>& out);
// Whether an instance's definition is still the one it applied from: its
// modifiers fold from the definition as it is, its tags and timing did not.
[[nodiscard]] bool IsEffectInstanceCurrent(const World& world, EntityId instance);

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
