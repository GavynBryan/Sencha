#pragma once

#include <abilities/AbilityCapacity.h>
#include <abilities/AbilityId.h>
#include <abilities/AbilityLocals.h>
#include <abilities/ActiveAbilities.h>
#include <core/handle/HandlePool.h>
#include <ecs/EntityId.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class World;

// One running or lingering activation, off the actor's row: an actor pays for what
// it is running rather than for everything it could run.
struct AbilityActivation
{
    EntityId Owner;
    AbilityRecord Record;
    AbilityRecordLocals Locals;
};

// Every activation in a World, a World resource. ActiveAbilities' slots name them.
//
// ActiveAbilities is replicated, and a replicated component may declare no lifecycle
// hooks, so a slot's activation is not freed by one. A pass collects any whose owner
// is gone or no longer names it. Nothing else depends on when that happens.
//
// Freed activations are kept for the next start, up to the larger of kMinSpare and a
// kSpareShareOfLive-th of those live, so warmed-up combat allocates nothing.
class AbilityActivations
{
public:
    static constexpr std::size_t kMinSpare = 64;
    static constexpr std::size_t kSpareShareOfLive = 4;

    struct Counters
    {
        std::size_t Live = 0;
        std::size_t Spare = 0;
        std::size_t PeakLive = 0;
        std::uint64_t Allocated = 0;
        std::uint64_t Reused = 0;
    };

    // A vacant slot reads as a Free record and empty locals.
    [[nodiscard]] AbilityRecord& Record(const ActiveAbilities& active, std::size_t slot);
    [[nodiscard]] const AbilityRecord& Record(const ActiveAbilities& active, std::size_t slot) const;
    [[nodiscard]] AbilityRecordLocals& Locals(const ActiveAbilities& active, std::size_t slot);
    [[nodiscard]] const AbilityRecordLocals& Locals(const ActiveAbilities& active, std::size_t slot) const;

    // The slot's activation, made if the slot was vacant and otherwise left as it is.
    AbilityActivation& Ensure(ActiveAbilities& active, std::size_t slot, EntityId owner);
    // The same, wiped: a new activation in that slot.
    AbilityActivation& Claim(ActiveAbilities& active, std::size_t slot, EntityId owner);
    void Free(ActiveAbilities& active, std::size_t slot);

    void CollectOrphans(const World& world);

    [[nodiscard]] Counters Stats() const { return Counts; }

private:
    using Handles = HandlePool<AbilityActivationTag, AbilityActivation>;

    void Retire(AbilityActivationHandle handle);

    Handles Pool;
    std::vector<std::unique_ptr<AbilityActivation>> Spare;
    std::vector<AbilityActivationHandle> Orphans;
    AbilityRecord VacantRecord;
    AbilityRecordLocals VacantLocals;
    Counters Counts;
};

// Whether `actor` has a live activation of `ability`.
[[nodiscard]] bool IsAbilityLive(const World& world, EntityId actor, AbilityId ability);

// What an actor's slots hold, by value, for tools and tests rather than per-tick code.
[[nodiscard]] std::array<AbilityRecord, kAbilitySlotCapacity> AbilityRecordsOf(const World& world, EntityId actor);
