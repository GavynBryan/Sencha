#include <abilities/AbilityActivations.h>

#include <ecs/World.h>

#include <algorithm>
#include <cassert>
#include <utility>

AbilityRecord& AbilityActivations::Record(const ActiveAbilities& active, std::size_t slot)
{
    AbilityActivation* activation = Pool.Find(active.Slots[slot]);
    return activation != nullptr ? activation->Record : VacantRecord;
}

const AbilityRecord& AbilityActivations::Record(const ActiveAbilities& active, std::size_t slot) const
{
    const AbilityActivation* activation = Pool.Find(active.Slots[slot]);
    return activation != nullptr ? activation->Record : VacantRecord;
}

AbilityRecordLocals& AbilityActivations::Locals(const ActiveAbilities& active, std::size_t slot)
{
    AbilityActivation* activation = Pool.Find(active.Slots[slot]);
    return activation != nullptr ? activation->Locals : VacantLocals;
}

const AbilityRecordLocals& AbilityActivations::Locals(const ActiveAbilities& active, std::size_t slot) const
{
    const AbilityActivation* activation = Pool.Find(active.Slots[slot]);
    return activation != nullptr ? activation->Locals : VacantLocals;
}

AbilityActivation& AbilityActivations::Ensure(ActiveAbilities& active, std::size_t slot, EntityId owner)
{
    assert(VacantRecord.State == AbilityRecordState::Free && VacantRecord.Identity == 0
           && "something wrote through a vacant slot");
    if (AbilityActivation* held = Pool.Find(active.Slots[slot]))
        return *held;
    if (!Spare.empty())
    {
        active.Slots[slot] = Pool.Adopt(std::move(Spare.back()));
        Spare.pop_back();
        ++Counts.Reused;
    }
    else
    {
        active.Slots[slot] = Pool.Emplace();
        ++Counts.Allocated;
    }
    ++Counts.Live;
    Counts.PeakLive = std::max(Counts.PeakLive, Counts.Live);
    Counts.Spare = Spare.size();
    AbilityActivation& activation = *Pool.Find(active.Slots[slot]);
    activation = AbilityActivation{};
    activation.Owner = owner;
    return activation;
}

AbilityActivation& AbilityActivations::Claim(ActiveAbilities& active, std::size_t slot, EntityId owner)
{
    AbilityActivation& activation = Ensure(active, slot, owner);
    activation = AbilityActivation{};
    activation.Owner = owner;
    return activation;
}

void AbilityActivations::Free(ActiveAbilities& active, std::size_t slot)
{
    Retire(active.Slots[slot]);
    active.Slots[slot] = {};
}

void AbilityActivations::Retire(AbilityActivationHandle handle)
{
    std::unique_ptr<AbilityActivation> value = Pool.Release(handle);
    if (value == nullptr)
        return;
    --Counts.Live;
    Spare.push_back(std::move(value));
    const std::size_t limit = kRetention.Limit(Counts.Live);
    if (Spare.size() > limit)
        Spare.resize(limit);
    Counts.Spare = Spare.size();
}

void AbilityActivations::CollectOrphans(const World& world)
{
    if (Counts.Live == 0)
        return;
    Orphans.clear();
    std::as_const(Pool).ForEach([&](AbilityActivationHandle handle, const AbilityActivation& activation) {
        const ActiveAbilities* active = world.IsAlive(activation.Owner) ? world.TryGet<ActiveAbilities>(activation.Owner)
                                                                       : nullptr;
        if (active == nullptr || std::ranges::find(active->Slots, handle) == std::ranges::end(active->Slots))
            Orphans.push_back(handle);
    });
    for (const AbilityActivationHandle orphan : Orphans)
        Retire(orphan);
}

bool IsAbilityLive(const World& world, EntityId actor, AbilityId ability)
{
    const ActiveAbilities* active = world.TryGet<ActiveAbilities>(actor);
    const AbilityActivations* activations = world.TryGetResource<AbilityActivations>();
    if (active == nullptr || activations == nullptr || !ability.IsValid())
        return false;
    for (std::size_t slot = 0; slot < kAbilitySlotCapacity; ++slot)
    {
        const AbilityRecord& record = activations->Record(*active, slot);
        if (record.State == AbilityRecordState::Live && record.Key.Ability == ability)
            return true;
    }
    return false;
}

std::array<AbilityRecord, kAbilitySlotCapacity> AbilityRecordsOf(const World& world, EntityId actor)
{
    std::array<AbilityRecord, kAbilitySlotCapacity> records{};
    const ActiveAbilities* active = world.TryGet<ActiveAbilities>(actor);
    const AbilityActivations* activations = world.TryGetResource<AbilityActivations>();
    if (active == nullptr || activations == nullptr)
        return records;
    for (std::size_t slot = 0; slot < kAbilitySlotCapacity; ++slot)
        records[slot] = activations->Record(*active, slot);
    return records;
}
