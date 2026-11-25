#pragma once

#include <ecs/ComponentAnnotations.h>
#include <ecs/EntityId.h>
#include <effects/EffectSystem.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

class StoragePartitionSet;
class World;
struct IComponentSerializer;

inline constexpr std::uint8_t kAppliedEffectCapacity = 16;

// The lasting effects an entity is under that a save keeps, and those content
// authors it with. The effect entities are the working state: this row holds
// entries only between a load and the entity's first tick.
struct SENCHA_COMPONENT("sencha.applied_effects") SENCHA_SAVED AppliedEffects
{
    std::uint8_t Count = 0;
    KeptEffect Entries[kAppliedEffectCapacity]{};
};

static_assert(std::is_trivially_copyable_v<AppliedEffects>);

// Before a capture, in a game that keeps level state: gives every identified
// entity under a kept effect the row a save writes its effects from, and reads
// what each is under. Answers how many kept effects a full row left out, each logged.
std::size_t SettleAppliedEffects(World& world);

// Identified entities under more kept effects than a row holds.
[[nodiscard]] std::vector<EntityId> OverfullAppliedEffects(const World& world);

// The first tick a loaded row's entity is active: each entry is put back, and the row emptied.
void RestoreAppliedEffects(World& world, const FixedSimTime& now, const StoragePartitionSet& partitions);

[[nodiscard]] std::unique_ptr<IComponentSerializer> MakeAppliedEffectsSerializer();

#if !defined(SENCHA_CODEGEN)
#  include <effects/AppliedEffects.sencha.h>
#endif
