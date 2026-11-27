#include <equipment/EquipmentSystem.h>

#include <abilities/AbilityActivations.h>
#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrants.h>
#include <app/GameContexts.h>
#include <core/hash/Fnv1a.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <equipment/Equipment.h>
#include <equipment/EquipmentComponents.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <world/held/HeldRecordIndex.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistableRef.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace
{
    struct EquipmentPassState
    {
        explicit EquipmentPassState(const World& world)
            : Wearers(world)
        {
        }

        struct RebindScan
        {
            EntityId Wearer;
            std::uint32_t Held = 0;
            std::uint64_t Records = 0;
        };

        Query<Read<EquipmentState>> Wearers;
        std::vector<EntityId> Scratch;
        std::vector<RebindScan> Scans;
        std::vector<RebindScan> NextScans;
        std::uint64_t Walks = 0;
        std::uint64_t Items = 0;
        std::uint64_t Layouts = 0;
        std::uint64_t Effects = 0;
        std::uint64_t Grants = 0;
    };

    template <typename Resource>
    std::uint64_t GenerationOf(const World& world)
    {
        const Resource* resource = world.TryGetResource<Resource>();
        return resource != nullptr ? resource->Generation() : 0;
    }

    bool DefinitionsMoved(const World& world, EquipmentPassState& state)
    {
        const std::uint64_t items = GenerationOf<ItemRegistry>(world);
        const std::uint64_t layouts = GenerationOf<EquipmentLayoutRegistry>(world);
        const std::uint64_t effects = GenerationOf<EffectRegistry>(world);
        const std::uint64_t grants = GenerationOf<AbilityEffectGrants>(world);
        const bool moved =
            items != state.Items || layouts != state.Layouts || effects != state.Effects || grants != state.Grants;
        state.Items = items;
        state.Layouts = layouts;
        state.Effects = effects;
        state.Grants = grants;
        return moved;
    }

    EquipmentPassState& StateOf(World& world)
    {
        return world.HasResource<EquipmentPassState>() ? world.GetResource<EquipmentPassState>()
                                                        : world.AddResource<EquipmentPassState>(world);
    }

    bool IsSourceOf(const World& world, std::span<const EquipmentRecord> records, EntityId entry)
    {
        return std::ranges::any_of(records, [&](const EquipmentRecord& record) {
            return ResolvePersistableRef(world, record.Source) == entry;
        });
    }

    SlotDepletion OnDepleted(const EquipmentLayout* layout, const EquipmentRecord& record)
    {
        return layout != nullptr && record.Primary < layout->Slots.size() ? layout->Slots[record.Primary].OnDepleted
                                                                         : SlotDepletion::Release;
    }

    std::span<const EquipmentRecord> Worn(const World& world, const EquipmentRecordLists& lists, EntityId wearer)
    {
        return lists.Read(world.TryGet<EquipmentState>(wearer)->Records);
    }

    bool Waits(const EquipmentRecord& record)
    {
        return !record.Source.Entity.IsValid() && !record.Source.Target.IsValid();
    }

    bool Awaits(const EquipmentRecord& record)
    {
        return !record.Source.Entity.IsValid() && record.Source.Target.IsValid();
    }

    void SettleSources(World& world, const EquipmentRecordLists& lists, EntityId wearer)
    {
        const EquipmentLayout* layout = FindEquipmentLayout(world, std::as_const(world).TryGet<EquipmentState>(wearer)->Layout);
        std::span<const EquipmentRecord> records = Worn(world, lists, wearer);
        for (std::size_t i = records.size(); i-- > 0;)
        {
            const EquipmentRecord record = records[i];
            if (Waits(record))
                continue;
            const EntityId source = ResolvePersistableRef(world, record.Source);
            if (!world.IsAlive(source) && Awaits(record))
                continue;
            if (world.IsAlive(source) && source != record.Source.Entity)
            {
                EquipmentRecord& arrived = EditEquipmentRecords(world, wearer)[i];
                arrived.Source.Entity = source;
                arrived.Derivation = EquipmentDerivation::Pending;
                records = Worn(world, lists, wearer);
            }
            const ItemEntry* entry = world.IsAlive(source) ? std::as_const(world).TryGet<ItemEntry>(source) : nullptr;
            const bool held = entry != nullptr && MayUseItem(world, wearer, source);
            if (held && entry->Count > 0)
                continue;
            if (held && HasLiveActivationFrom(world, wearer, source))
                continue;
            if (held && OnDepleted(layout, record) == SlotDepletion::Hold)
            {
                EndMaintained(world, record);
                EditEquipmentRecords(world, wearer)[i].Source = PersistableRef{};
                records = Worn(world, lists, wearer);
                continue;
            }
            EndEquipmentRecord(world, wearer, i);
            records = Worn(world, lists, wearer);
        }
    }

    std::uint64_t RecordsFingerprint(std::span<const EquipmentRecord> records)
    {
        std::uint64_t hash = kFnv1aOffsetBasis;
        for (const EquipmentRecord& record : records)
        {
            HashFnv1aValue(hash, record.Key);
            HashFnv1aValue(hash, record.Source.Entity.Index);
            HashFnv1aValue(hash, record.Source.Entity.Generation);
            HashFnv1aValue(hash, record.Source.Target);
        }
        return hash;
    }

    void Rebind(World& world, const EquipmentRecordLists& lists, EntityId wearer, EquipmentPassState& pass)
    {
        std::span<const EquipmentRecord> records = Worn(world, lists, wearer);
        if (std::ranges::none_of(records, Waits))
            return;
        const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
        const std::uint32_t held = index != nullptr ? index->Revision(wearer) : 0u;
        const auto last = std::ranges::lower_bound(pass.Scans, wearer, {}, &EquipmentPassState::RebindScan::Wearer);
        if (last != pass.Scans.end() && last->Wearer == wearer && last->Held == held
            && last->Records == RecordsFingerprint(records))
        {
            pass.NextScans.push_back(*last);
            return;
        }
        const std::vector<EntityId> entries = HeldEntries(world, wearer);
        ++pass.Walks;
        for (std::size_t i = 0; i < records.size(); ++i)
        {
            if (!Waits(records[i]))
                continue;
            const ItemId definition = records[i].Definition;
            for (const EntityId entry : entries)
            {
                const ItemEntry& candidate = *std::as_const(world).TryGet<ItemEntry>(entry);
                if (candidate.Definition != definition || candidate.Count == 0 || IsSourceOf(world, records, entry))
                    continue;
                const PersistableRef source = MakePersistableRef(world, entry);
                EquipmentRecord& rebound = EditEquipmentRecords(world, wearer)[i];
                rebound.Source = source;
                rebound.Derivation = EquipmentDerivation::Pending;
                records = Worn(world, lists, wearer);
                break;
            }
        }
        pass.NextScans.push_back(
            { .Wearer = wearer, .Held = held, .Records = RecordsFingerprint(records) });
    }

    bool AdmitMove(const World& world, EntityId entry)
    {
        const EntityId holder = ItemHolder(world, entry);
        return std::ranges::none_of(EquipmentRecordsOf(world, holder), [&](const EquipmentRecord& record) {
            return ItemIsWithin(world, ResolvePersistableRef(world, record.Source), entry)
                && EquipmentRecordLocked(world, holder, record);
        });
    }

    void Moved(World& world, EntityId, EntityId formerHolder)
    {
        if (world.IsAlive(formerHolder) && world.IsRegistered<EquipmentState>()
            && world.HasComponent<EquipmentState>(formerHolder))
            if (const EquipmentRecordLists* lists = world.TryGetResource<EquipmentRecordLists>())
                SettleSources(world, *lists, formerHolder);
    }
}

bool InstallEquipmentMoveAdmission(World& world)
{
    return InstallItemMoveAdmission(world, ItemMoveAdmission{ .Admit = &AdmitMove, .Moved = &Moved });
}

bool HoldsEquipmentMoveAdmission(const World& world)
{
    const ItemMoveAdmission* admission = world.TryGetResource<ItemMoveAdmission>();
    return admission != nullptr && admission->Admit == &AdmitMove && admission->Moved == &Moved;
}

void RunEquipmentPass(World& world, const FixedSimTime& now)
{
    if (!world.IsRegistered<EquipmentState>() || !IsSimulationAuthority(world))
        return;
    EquipmentPassState& state = StateOf(world);
    EquipmentRecordLists* lists = world.TryGetResource<EquipmentRecordLists>();
    if (lists == nullptr)
        return;
    lists->DropUnreferenced();
    state.Scratch.clear();
    state.Wearers.ForEachChunk([&](auto& view) {
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            if (view.template Read<EquipmentState>()[row].Count > 0)
                state.Scratch.push_back(view.Entity(row));
    });
    const bool moved = DefinitionsMoved(world, state);
    state.NextScans.clear();
    for (const EntityId wearer : state.Scratch)
    {
        if (moved)
            for (EquipmentRecord& record : EditEquipmentRecords(world, wearer))
                record.Derivation = EquipmentDerivation::Pending;
        SettleSources(world, *lists, wearer);
        Rebind(world, *lists, wearer, state);
        DeriveDueEquipmentRecords(world, wearer, now);
    }
    std::ranges::sort(state.NextScans, {}, &EquipmentPassState::RebindScan::Wearer);
    std::swap(state.Scans, state.NextScans);
}

std::uint64_t EquipmentRebindWalks(const World& world)
{
    const EquipmentPassState* state = world.TryGetResource<EquipmentPassState>();
    return state != nullptr ? state->Walks : 0;
}

void EquipmentSystem::FixedLogic(FixedLogicContext& ctx)
{
    RunEquipmentPass(ctx.Entities, ctx.Time);
}
