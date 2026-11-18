#include <equipment/Equipment.h>

#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityTrace.h>
#include <core/hash/Fnv1a.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <equipment/EquipmentComponents.h>
#include <equipment/WornGrantEntries.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentIdMint.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <span>
#include <string>
#include <vector>

namespace
{
    EquipmentResult Refused(EquipmentRefusal refusal) { return { .Refusal = refusal, .Record = 0 }; }

    const EffectDefinition* EffectOf(const World& world, EffectId effect)
    {
        const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
        return effects != nullptr ? effects->Get(effect) : nullptr;
    }

    // Owned by the handle the record last held, alive or not, and by whatever its source resolves to now.
    std::vector<EntityId> Maintained(const World& world, const EquipmentRecord& record)
    {
        std::vector<EntityId> effects;
        if (record.Source.Entity.IsValid())
            FindEffectsOwnedBy(world, EquipmentRecordOwner(record.Source.Entity, record.Key), effects);
        const EntityId source = ResolvePersistableRef(world, record.Source);
        if (source.IsValid() && source != record.Source.Entity)
            FindEffectsOwnedBy(world, EquipmentRecordOwner(source, record.Key), effects);
        return effects;
    }

    std::size_t GrantsWanted(const World& world, std::span<const ItemEquipEffect> effects)
    {
        const AbilityEffectGrants* table = world.TryGetResource<AbilityEffectGrants>();
        std::size_t grants = 0;
        for (const ItemEquipEffect& equipped : effects)
            grants += table != nullptr ? table->Of(equipped.Effect).size() : 0;
        return grants;
    }

    // An Instant effect would write its modifiers into base at every apply.
    bool IsMaintainable(const World& world, EffectId effect)
    {
        const EffectDefinition* definition = EffectOf(world, effect);
        return definition != nullptr && definition->Duration == EffectDuration::Infinite;
    }

    EntityId Maintain(World& world, EntityId wearer, EntityId source, std::uint32_t record, const EquipmentSlot& slot,
                      const ItemDefinition& item, const ItemEquipEffect& equipped, const FixedSimTime& now)
    {
        std::vector<std::uint16_t> worn;
        if (!IsMaintainable(world, equipped.Effect) || !FindWornGrantEntries(world, equipped.Effect, slot, item, worn))
            return {};
        const EntityId instance =
            ApplyEffect(world, wearer, equipped.Effect, now, EquipmentRecordOwner(source, record), equipped.Scale);
        if (instance.IsValid() && !worn.empty())
            world.GetResource<AbilityEffectGrants>().SetInstance(instance, std::move(worn));
        return instance;
    }

    std::size_t RecordHolding(const EquipmentState& state, std::uint8_t slot)
    {
        for (std::size_t i = 0; i < state.Count; ++i)
            if ((state.Records[i].Slots >> slot) & 1u)
                return i;
        return kEquipmentRecordCapacity;
    }

    std::uint32_t HeldSlots(const EquipmentState& state)
    {
        std::uint32_t held = 0;
        for (std::size_t i = 0; i < state.Count; ++i)
            held |= state.Records[i].Slots;
        return held;
    }

    bool HoldsAny(const World& world, EntityId entity, std::span<const GameplayTagId> wanted)
    {
        const GameplayTagContainer* tags = world.IsAlive(entity) && world.IsRegistered<GameplayTagContainer>()
            ? world.TryGet<GameplayTagContainer>(entity)
            : nullptr;
        return tags != nullptr && std::ranges::any_of(wanted, [&](GameplayTagId tag) { return tags->HasExact(tag); });
    }

    const EquipmentState* StateOf(const World& world, EntityId wearer)
    {
        return world.IsAlive(wearer) && world.IsRegistered<EquipmentState>() ? world.TryGet<EquipmentState>(wearer)
                                                                             : nullptr;
    }
}

EquipmentRefusal ResolveEquipmentSlots(const World& world, const EquipmentLayout& layout, const EquipmentState& state,
                                       const ItemDefinition& item, std::string_view asked, std::uint32_t& mask,
                                       std::uint8_t& primary)
{
    mask = 0;
    primary = kNoEquipmentSlot;
    const std::uint32_t held = HeldSlots(state);
    for (std::size_t n = 0; n < item.Occupies.size(); ++n)
    {
        const std::vector<std::uint8_t> candidates = EquipmentSlotsNamed(layout, item.Occupies[n]);
        std::uint8_t chosen = kNoEquipmentSlot;
        if (n == 0 && !asked.empty())
        {
            const std::uint8_t slot = FindEquipmentSlot(layout, asked);
            if (std::ranges::find(candidates, slot) != candidates.end())
                chosen = slot;
        }
        else
        {
            const auto free = [&](std::uint8_t slot) { return !((mask >> slot) & 1u) && !((held >> slot) & 1u); };
            const auto untaken = [&](std::uint8_t slot) { return !((mask >> slot) & 1u); };
            if (const auto found = std::ranges::find_if(candidates, free); found != candidates.end())
                chosen = *found;
            else if (const auto any = std::ranges::find_if(candidates, untaken); any != candidates.end())
                chosen = *any;
        }
        if (chosen == kNoEquipmentSlot)
            return EquipmentRefusal::NoSlot;
        if (!ItemCarries(world, item, layout.Slots[chosen].Accepts))
            return EquipmentRefusal::NotAccepted;
        mask |= 1u << chosen;
        if (n == 0)
            primary = chosen;
    }
    return EquipmentRefusal::None;
}

void EndMaintained(World& world, const EquipmentRecord& record)
{
    for (const EntityId effect : Maintained(world, record))
        (void)RemoveEffect(world, effect);
}

void EndEquipmentRecord(World& world, EntityId wearer, std::size_t index)
{
    // Ending effects can move the wearer's row.
    EndMaintained(world, world.TryGet<EquipmentState>(wearer)->Records[index]);
    EquipmentState& state = *world.TryGet<EquipmentState>(wearer);
    state.Records[index] = state.Records[state.Count - 1];
    state.Records[--state.Count] = EquipmentRecord{};
}

bool EquipmentWearerLocked(const World& world, EntityId wearer)
{
    const EquipmentState* state = StateOf(world, wearer);
    const EquipmentLayout* layout = state != nullptr ? FindEquipmentLayout(world, state->Layout) : nullptr;
    return layout != nullptr && HoldsAny(world, wearer, layout->LockTags);
}

bool EquipmentRecordLocked(const World& world, EntityId wearer, const EquipmentRecord& record)
{
    if (EquipmentWearerLocked(world, wearer))
        return true;
    const ItemDefinition* item = FindItemDefinition(world, record.Definition);
    return item != nullptr && HoldsAny(world, ResolvePersistableRef(world, record.Source), item->EquipLockTags);
}

LeaseOwner EquipmentRecordOwner(EntityId source, std::uint32_t record)
{
    std::uint64_t key = kFnv1aOffsetBasis;
    constexpr std::string_view kOwnerName = "equipment.record";
    HashFnv1aBytes(key, kOwnerName.data(), kOwnerName.size());
    HashFnv1aValue(key, record);
    return LeaseOwner{ .Entity = source, .Key = key };
}

EquipmentResult Equipment::Equip(EntityId wearer, EntityId entry, std::string_view slot)
{
    World& world = *Entities;
    if (!IsSimulationAuthority(world))
        return Refused(EquipmentRefusal::NotAuthority);
    const EquipmentState* state = StateOf(world, wearer);
    const EquipmentLayout* layout = state != nullptr ? FindEquipmentLayout(world, state->Layout) : nullptr;
    const ItemEntry* held = world.IsAlive(entry) ? std::as_const(world).TryGet<ItemEntry>(entry) : nullptr;
    const ItemDefinition* item = held != nullptr ? FindItemDefinition(world, held->Definition) : nullptr;
    if (layout == nullptr || item == nullptr || held->Count == 0)
        return Refused(EquipmentRefusal::Missing);
    if (!MayUseItem(world, wearer, entry))
        return Refused(EquipmentRefusal::NotPermitted);
    if (item->Occupies.empty())
        return Refused(EquipmentRefusal::NotEquippable);
    for (std::size_t i = 0; i < state->Count; ++i)
        if (ResolvePersistableRef(world, state->Records[i].Source) == entry)
            return Refused(EquipmentRefusal::AlreadyEquipped);
    if (EquipmentWearerLocked(world, wearer))
        return Refused(EquipmentRefusal::Locked);

    std::uint32_t slots = 0;
    std::uint8_t primary = kNoEquipmentSlot;
    if (const EquipmentRefusal refusal = ResolveEquipmentSlots(world, *layout, *state, *item, slot, slots, primary);
        refusal != EquipmentRefusal::None)
        return Refused(refusal);

    // Checked against the state after what it replaces has ended.
    std::vector<std::size_t> replaced;
    std::vector<EntityId> ending;
    for (std::size_t i = 0; i < state->Count; ++i)
        if (state->Records[i].Slots & slots)
        {
            if (EquipmentRecordLocked(world, wearer, state->Records[i]))
                return Refused(EquipmentRefusal::Locked);
            replaced.push_back(i);
            for (const EntityId effect : Maintained(world, state->Records[i]))
                ending.push_back(effect);
        }
    if (state->Count - replaced.size() + 1 > kEquipmentRecordCapacity)
        return Refused(EquipmentRefusal::Full);
    std::vector<std::uint16_t> worn;
    for (const ItemEquipEffect& equipped : item->EquipEffects)
    {
        if (!IsMaintainable(world, equipped.Effect))
            return Refused(EquipmentRefusal::NotMaintained);
        if (!FindWornGrantEntries(world, equipped.Effect, layout->Slots[primary], *item, worn))
        {
            assert(false && "an item's worn grant entries are declared with content (DeclareWornGrantEntries)");
            return Refused(EquipmentRefusal::Undeclared);
        }
    }
    if (GrantsWanted(world, item->EquipEffects) > AbilityGrantRoom(world, wearer, ending))
        return Refused(EquipmentRefusal::GrantRoom);

    // Minting the entry's id moves the entry's row, so its definition is read first.
    const ItemId definition = held->Definition;
    const PersistableRef source{ entry, EnsurePersistentId(world, entry) };
    for (auto it = replaced.rbegin(); it != replaced.rend(); ++it)
        EndEquipmentRecord(world, wearer, *it);
    EquipmentState& wearing = *world.TryGet<EquipmentState>(wearer);
    const EquipmentRecord record{ .Key = wearing.NextKey++,
                                  .Definition = definition,
                                  .Slots = slots,
                                  .Primary = primary,
                                  .Source = source,
                                  .Derivation = EquipmentDerivation::Maintained };
    wearing.Records[wearing.Count++] = record;
    for (const ItemEquipEffect& equipped : item->EquipEffects)
    {
        [[maybe_unused]] const EntityId instance =
            Maintain(world, wearer, entry, record.Key, layout->Slots[primary], *item, equipped, Now);
        assert(instance.IsValid() && "an equip the check admitted cannot be refused by its apply");
    }
    return { .Refusal = EquipmentRefusal::None, .Record = record.Key };
}

EquipmentResult Equipment::Unequip(EntityId wearer, std::string_view slot, ItemChangeClass change)
{
    World& world = *Entities;
    if (!IsSimulationAuthority(world))
        return Refused(EquipmentRefusal::NotAuthority);
    const EquipmentState* state = StateOf(world, wearer);
    const EquipmentLayout* layout = state != nullptr ? FindEquipmentLayout(world, state->Layout) : nullptr;
    const std::uint8_t index = layout != nullptr ? FindEquipmentSlot(*layout, slot) : kNoEquipmentSlot;
    if (index == kNoEquipmentSlot)
        return Refused(EquipmentRefusal::NoSlot);
    const std::size_t record = RecordHolding(*state, index);
    if (record == kEquipmentRecordCapacity)
        return Refused(EquipmentRefusal::Missing);
    if (change == ItemChangeClass::Requested && EquipmentRecordLocked(world, wearer, state->Records[record]))
        return Refused(EquipmentRefusal::Locked);
    const std::uint32_t key = state->Records[record].Key;
    EndEquipmentRecord(world, wearer, record);
    return { .Refusal = EquipmentRefusal::None, .Record = key };
}

EquipmentResult Equipment::Cycle(EntityId wearer, std::string_view group)
{
    World& world = *Entities;
    if (!IsSimulationAuthority(world))
        return Refused(EquipmentRefusal::NotAuthority);
    const EquipmentState* state = StateOf(world, wearer);
    const EquipmentLayout* layout = state != nullptr ? FindEquipmentLayout(world, state->Layout) : nullptr;
    const std::vector<std::uint8_t> slots = layout != nullptr ? EquipmentSlotsNamed(*layout, group) : std::vector<std::uint8_t>{};
    if (slots.empty())
        return Refused(EquipmentRefusal::NoSlot);
    const EquipmentSlot& first = layout->Slots[slots.front()];
    const std::size_t holding = RecordHolding(*state, slots.front());
    const EntityId current =
        holding < kEquipmentRecordCapacity ? ResolvePersistableRef(world, state->Records[holding].Source) : EntityId{};

    std::vector<EntityId> fits;
    for (const EntityId entry : HeldEntries(world, wearer))
    {
        const ItemEntry& candidate = *world.TryGet<ItemEntry>(entry);
        const ItemDefinition* item = FindItemDefinition(world, candidate.Definition);
        bool wornElsewhere = false;
        for (std::size_t i = 0; i < state->Count; ++i)
            wornElsewhere = wornElsewhere
                || (i != holding && ResolvePersistableRef(world, state->Records[i].Source) == entry);
        const std::vector<std::uint8_t> named = item != nullptr && !item->Occupies.empty()
            ? EquipmentSlotsNamed(*layout, item->Occupies.front())
            : std::vector<std::uint8_t>{};
        const bool covers = std::ranges::find(named, slots.front()) != named.end();
        if (entry == current
            || (candidate.Count > 0 && covers && ItemCarries(world, *item, first.Accepts) && !wornElsewhere))
            fits.push_back(entry);
    }
    // From the one after the current, wrapping; a refused equip changes nothing.
    const auto at = std::ranges::find(fits, current);
    const std::size_t start = at == fits.end() ? 0 : static_cast<std::size_t>(at - fits.begin()) + 1;
    const std::string key = first.Key;
    for (std::size_t n = 0; n < fits.size(); ++n)
    {
        const EntityId next = fits[(start + n) % fits.size()];
        if (next == current)
            continue;
        if (const EquipmentResult equipped = Equip(wearer, next, key); equipped.Refusal == EquipmentRefusal::None)
            return equipped;
    }
    if (current.IsValid() || holding < kEquipmentRecordCapacity)
        return Unequip(wearer, key);
    return Refused(EquipmentRefusal::Missing);
}

namespace
{
    std::size_t RecordIndex(const EquipmentState& state, std::uint32_t key)
    {
        for (std::size_t i = 0; i < state.Count; ++i)
            if (state.Records[i].Key == key)
                return i;
        return kEquipmentRecordCapacity;
    }

    bool Representable(const World& world, const EquipmentLayout& layout, const ItemDefinition& item,
                       const EquipmentRecord& record)
    {
        const std::size_t size = layout.Slots.size();
        if (record.Primary >= size || !((record.Slots >> record.Primary) & 1u) || item.Occupies.empty()
            || (size < kMaxEquipmentSlots && (record.Slots >> size) != 0)
            || static_cast<std::size_t>(std::popcount(record.Slots)) != item.Occupies.size())
            return false;
        const std::vector<std::uint8_t> first = EquipmentSlotsNamed(layout, item.Occupies.front());
        if (std::ranges::find(first, record.Primary) == first.end())
            return false;
        for (std::uint32_t mask = record.Slots; mask != 0; mask &= mask - 1)
            if (!ItemCarries(world, item, layout.Slots[std::countr_zero(mask)].Accepts))
                return false;
        return true;
    }

    EquipmentDerivation Derive(World& world, EntityId wearer, std::size_t index, const FixedSimTime& now)
    {
        const EquipmentRecord record = world.TryGet<EquipmentState>(wearer)->Records[index];
        const EquipmentLayout* layout = FindEquipmentLayout(world, std::as_const(world).TryGet<EquipmentState>(wearer)->Layout);
        const ItemDefinition* item = FindItemDefinition(world, record.Definition);
        if (layout == nullptr || item == nullptr)
        {
            EndMaintained(world, record);
            return EquipmentDerivation::RefusedByContent;
        }
        if (!Representable(world, *layout, *item, record))
        {
            EndEquipmentRecord(world, wearer, index);
            return EquipmentDerivation::Pending;
        }
        const EquipmentSlot& slot = layout->Slots[record.Primary];
        const EntityId source = ResolvePersistableRef(world, record.Source);
        const std::vector<ItemEquipEffect> want = source.IsValid() ? item->EquipEffects : std::vector<ItemEquipEffect>{};
        std::vector<std::vector<std::uint16_t>> worn(want.size());
        for (std::size_t w = 0; w < want.size(); ++w)
            if (!IsMaintainable(world, want[w].Effect) || !FindWornGrantEntries(world, want[w].Effect, slot, *item, worn[w]))
            {
                EndMaintained(world, record);
                return EquipmentDerivation::RefusedByContent;
            }

        // Kept: one instance per wanted effect, of its effect and scale, still current.
        const std::vector<EntityId> have = Maintained(world, record);
        std::vector<EntityId> kept(want.size());
        std::vector<EntityId> extra;
        for (const EntityId instance : have)
        {
            const ActiveEffect& active = *std::as_const(world).TryGet<ActiveEffect>(instance);
            std::size_t w = 0;
            while (w < want.size()
                   && (kept[w].IsValid() || want[w].Effect != active.Def || want[w].Scale != active.Scale
                       || !IsEffectInstanceCurrent(world, instance)))
                ++w;
            if (w < want.size())
                kept[w] = instance;
            else
                extra.push_back(instance);
        }
        if (GrantsWanted(world, want) > AbilityGrantRoom(world, wearer, have))
            return EquipmentDerivation::RefusedForRoom;

        for (const EntityId instance : extra)
            (void)RemoveEffect(world, instance);
        AbilityEffectGrants* table = world.TryGetResource<AbilityEffectGrants>();
        for (std::size_t w = 0; w < want.size(); ++w)
        {
            if (kept[w].IsValid())
            {
                if (table != nullptr && !worn[w].empty())
                    table->SetInstance(kept[w], std::move(worn[w]));
                continue;
            }
            if (!Maintain(world, wearer, source, record.Key, slot, *item, want[w], now).IsValid())
            {
                EndMaintained(world, record);
                return EquipmentDerivation::RefusedForRoom;
            }
        }
        return EquipmentDerivation::Maintained;
    }

    bool IsRefused(EquipmentDerivation derivation)
    {
        return derivation == EquipmentDerivation::RefusedForRoom || derivation == EquipmentDerivation::RefusedByContent;
    }

    void DeriveRecord(World& world, EntityId wearer, std::uint32_t key, const FixedSimTime& now)
    {
        const EquipmentState* state = std::as_const(world).TryGet<EquipmentState>(wearer);
        const std::size_t index = state != nullptr ? RecordIndex(*state, key) : kEquipmentRecordCapacity;
        if (index == kEquipmentRecordCapacity)
            return;
        const EquipmentDerivation was = state->Records[index].Derivation;
        const EquipmentDerivation derived = Derive(world, wearer, index, now);
        const std::size_t at = RecordIndex(*std::as_const(world).TryGet<EquipmentState>(wearer), key);
        if (at == kEquipmentRecordCapacity)
            return;
        world.TryGet<EquipmentState>(wearer)->Records[at].Derivation = derived;
        if (IsRefused(derived) && derived != was)
            if (AbilityTrace* trace = world.TryGetResource<AbilityTrace>())
                trace->Record({ .Tick = now.TickIndex,
                                .Actor = wearer,
                                .Event = AbilityTraceEvent::GrantRefused,
                                .Refusal = derived == EquipmentDerivation::RefusedForRoom ? AbilityRefusal::Capacity
                                                                                       : AbilityRefusal::Undefined,
                                .Element = static_cast<std::uint16_t>(key) });
    }

    std::vector<std::uint32_t> RecordKeys(const World& world, EntityId wearer)
    {
        const EquipmentState* state = StateOf(world, wearer);
        std::vector<std::uint32_t> keys;
        for (std::size_t i = 0; state != nullptr && i < state->Count; ++i)
            keys.push_back(state->Records[i].Key);
        return keys;
    }
}

std::size_t ReconcileEquipment(World& world, EntityId wearer, const FixedSimTime& now)
{
    if (!IsSimulationAuthority(world))
        return 0;
    std::size_t refused = 0;
    for (const std::uint32_t key : RecordKeys(world, wearer))
    {
        DeriveRecord(world, wearer, key, now);
        const EquipmentState& state = *std::as_const(world).TryGet<EquipmentState>(wearer);
        const std::size_t index = RecordIndex(state, key);
        refused += index != kEquipmentRecordCapacity && IsRefused(state.Records[index].Derivation) ? 1 : 0;
    }
    return refused;
}

void DeriveDueEquipmentRecords(World& world, EntityId wearer, const FixedSimTime& now)
{
    for (const std::uint32_t key : RecordKeys(world, wearer))
    {
        const EquipmentState& state = *std::as_const(world).TryGet<EquipmentState>(wearer);
        const std::size_t index = RecordIndex(state, key);
        if (index == kEquipmentRecordCapacity)
            continue;
        const EquipmentRecord& record = state.Records[index];
        const ItemDefinition* item = FindItemDefinition(world, record.Definition);
        const bool due = record.Derivation == EquipmentDerivation::Pending
            || (record.Derivation == EquipmentDerivation::RefusedForRoom && item != nullptr
                && GrantsWanted(world, item->EquipEffects) <= AbilityGrantRoom(world, wearer));
        if (due)
            DeriveRecord(world, wearer, key, now);
    }
}

EquipmentLayoutId RedefineEquipmentLayout(World& world, std::string_view name, EquipmentLayout layout)
{
    EquipmentLayoutRegistry& layouts = world.GetResource<EquipmentLayoutRegistry>();
    const EquipmentLayoutId id = layouts.Find(name);
    const EquipmentLayout* was = id.IsValid() ? layouts.Get(id) : nullptr;
    // Each old slot's index in the new layout, by key; kNoEquipmentSlot where it went.
    std::vector<std::uint8_t> moved;
    if (was != nullptr)
        for (const EquipmentSlot& slot : was->Slots)
            moved.push_back(FindEquipmentSlot(layout, slot.Key));
    const EquipmentLayoutId defined = layouts.Define(name, std::move(layout));
    if (was == nullptr || !world.IsRegistered<EquipmentState>())
        return defined;

    std::vector<EntityId> wearers;
    Query<Read<EquipmentState>> query(world);
    query.ForEachChunk([&](auto& view) {
        const auto states = view.template Read<EquipmentState>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            if (states[row].Layout == defined && states[row].Count > 0)
                wearers.push_back(view.Entity(row));
    });
    for (const EntityId wearer : wearers)
        for (const std::uint32_t key : RecordKeys(world, wearer))
        {
            EquipmentState& state = *world.TryGet<EquipmentState>(wearer);
            const std::size_t index = RecordIndex(state, key);
            EquipmentRecord& record = state.Records[index];
            std::uint32_t slots = 0;
            bool kept = record.Primary < moved.size() && moved[record.Primary] != kNoEquipmentSlot;
            for (std::uint32_t mask = record.Slots; kept && mask != 0; mask &= mask - 1)
            {
                const std::size_t old = static_cast<std::size_t>(std::countr_zero(mask));
                kept = old < moved.size() && moved[old] != kNoEquipmentSlot;
                if (kept)
                    slots |= 1u << moved[old];
            }
            if (!kept)
            {
                EndEquipmentRecord(world, wearer, index);
                continue;
            }
            record.Slots = slots;
            record.Primary = moved[record.Primary];
            record.Derivation = EquipmentDerivation::Pending;
        }
    return defined;
}
