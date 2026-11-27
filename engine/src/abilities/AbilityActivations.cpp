#include <abilities/AbilityActivations.h>

#include <abilities/AbilityGrants.h>
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

namespace
{
    template <typename Match>
    bool AnyLiveRecord(const World& world, EntityId actor, Match&& match)
    {
        const AbilityActivations* activations = world.TryGetResource<AbilityActivations>();
        if (activations == nullptr || !world.IsRegistered<ActiveAbilities>())
            return false;
        const ActiveAbilities* active = world.TryGet<ActiveAbilities>(actor);
        if (active == nullptr)
            return false;
        for (std::size_t slot = 0; slot < kAbilitySlotCapacity; ++slot)
        {
            const AbilityRecord& record = activations->Record(*active, slot);
            if (record.State == AbilityRecordState::Live && match(record))
                return true;
        }
        return false;
    }
}

bool IsAbilityLive(const World& world, EntityId actor, AbilityId ability)
{
    return ability.IsValid()
        && AnyLiveRecord(world, actor, [&](const AbilityRecord& record) { return record.Key.Ability == ability; });
}

bool HasLiveActivationFrom(const World& world, EntityId actor, EntityId source)
{
    if (!source.IsValid() || !world.IsAlive(actor) || !world.IsRegistered<AbilityGrants>())
        return false;
    const AbilityGrants* grants = world.TryGet<AbilityGrants>(actor);
    return grants != nullptr && AnyLiveRecord(world, actor, [&](const AbilityRecord& record) {
        const AbilityGrant* grant = grants->Find(record.Grant);
        return grant != nullptr && grant->Source == source;
    });
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
