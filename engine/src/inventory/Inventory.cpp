#include <inventory/Inventory.h>

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityRequestQueue.h>
#include <attributes/AttributeSet.h>
#include <ecs/World.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventorySystem.h>
#include <world/SimulationAuthority.h>
#include <world/held/HeldRecordIndex.h>
#include <world/held/HeldRecords.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <utility>

namespace
{
    InventoryResult Refused(InventoryRefusal refusal) { return { .Refusal = refusal, .Entry = {} }; }

    const HeldRecordIndex* IndexOf(const World& world) { return world.TryGetResource<HeldRecordIndex>(); }

    const ItemEntry* EntryOf(const World& world, EntityId entry)
    {
        return world.IsAlive(entry) && world.IsRegistered<ItemEntry>() ? world.TryGet<ItemEntry>(entry) : nullptr;
    }

    const ItemContainer* ContainerOf(const World& world, EntityId container)
    {
        return world.IsAlive(container) ? world.TryGet<ItemContainer>(container) : nullptr;
    }

    EntityId HolderOf(const World& world, EntityId record)
    {
        const HeldRecordIndex* index = IndexOf(world);
        return index != nullptr ? index->HolderOf(record) : EntityId{};
    }

    std::uint32_t RoomOf(const ItemContainer& container)
    {
        return container.Capacity == 0 ? kItemContainerMostEntries : container.Capacity;
    }

    bool HasRoom(const World& world, const ItemContainer& container, EntityId containerEntity, std::size_t newEntries)
    {
        return ItemsIn(world, containerEntity).size() + newEntries <= RoomOf(container);
    }

    bool FitsDepth(const World& world, EntityId entry, EntityId into)
    {
        const HeldRecordIndex* index = IndexOf(world);
        const int height = entry.IsValid() && index != nullptr ? index->Height(entry) : 0;
        return (index != nullptr ? index->Depth(into) : 0) + 1 + height <= kHeldDeepestNesting;
    }

    // Not lying in a pickup, held by the world, or waiting for a holder that is away.
    bool Contained(const World& world, EntityId entry)
    {
        const EntityId holder = HolderOf(world, entry);
        return world.IsAlive(holder) && !world.HasComponent<ItemPickup>(holder);
    }

    bool AdmitsMove(const World& world, EntityId entry)
    {
        const ItemMoveAdmission* admission = world.TryGetResource<ItemMoveAdmission>();
        return admission == nullptr || admission->Admit == nullptr || admission->Admit(world, entry);
    }

    void TellMoved(World& world, EntityId entry, EntityId formerHolder)
    {
        const ItemMoveAdmission* admission = world.TryGetResource<ItemMoveAdmission>();
        if (admission != nullptr && admission->Moved != nullptr)
            admission->Moved(world, entry, formerHolder);
    }

    InventoryRefusal Admission(const World& world, EntityId entry, EntityId into, const ItemContainer& container,
                               const ItemDefinition& item)
    {
        if (!ItemCarries(world, item, container.Accepts) || ItemIsWithin(world, into, entry))
            return InventoryRefusal::NotAccepted;
        if (!FitsDepth(world, entry, into))
            return InventoryRefusal::TooDeep;
        return InventoryRefusal::None;
    }

    EntityId CreateEntry(World& world, EntityId container, ItemId item, const ItemDefinition& definition,
                         std::uint32_t count)
    {
        const bool attributes = !definition.Attributes.empty();
        const bool tags = (!definition.InstanceTags.empty() || !definition.EquipLockTags.empty())
            && world.IsRegistered<GameplayTagContainer>();
        ArchetypeSignature values;
        values.set(world.GetComponentId<ItemEntry>());
        if (attributes)
            values.set(world.GetComponentId<AttributeSet>());
        if (tags)
            values.set(world.GetComponentId<GameplayTagContainer>());
        const EntityId entry = PlaceHeldRecord(world, container, values);
        if (!entry.IsValid())
            return entry;
        (void)world.InitializeComponent<ItemEntry>(entry, ItemEntry{ .Definition = item, .Count = count });
        if (attributes)
        {
            AttributeSet seeded{};
            for (const ItemAttributeSeed& seed : definition.Attributes)
                (void)seeded.Add(seed.Attribute, seed.Base);
            (void)world.InitializeComponent<AttributeSet>(entry, seeded);
        }
        // A curse laid on an entry by an effect needs somewhere to land.
        if (tags)
        {
            GameplayTagContainer granted{};
            for (const GameplayTagId tag : definition.InstanceTags)
                (void)granted.GrantBaseline(tag);
            (void)world.InitializeComponent<GameplayTagContainer>(entry, granted);
        }
        return entry;
    }

    struct StackFill
    {
        std::vector<std::pair<EntityId, std::uint32_t>> Fills;
        std::uint32_t Remaining = 0;
    };

    // A depleted stack is never refilled.
    StackFill FillOpenStacks(const World& world, EntityId container, ItemId definition, const ItemDefinition& item,
                             std::uint32_t units, EntityId except)
    {
        StackFill fill{ .Fills = {}, .Remaining = units };
        if (item.StackMax <= 1)
            return fill;
        for (const EntityId entry : ItemsIn(world, container))
        {
            const ItemEntry& held = *world.TryGet<ItemEntry>(entry);
            if (fill.Remaining == 0)
                break;
            if (entry == except || held.Definition != definition || held.Count == 0 || held.Count >= item.StackMax)
                continue;
            const std::uint32_t added = std::min(fill.Remaining, item.StackMax - held.Count);
            fill.Fills.emplace_back(entry, added);
            fill.Remaining -= added;
        }
        return fill;
    }

    EntityId ApplyFills(World& world, const StackFill& fill)
    {
        EntityId last;
        for (const auto& [entry, added] : fill.Fills)
        {
            SetItemCount(world, entry, std::as_const(world).TryGet<ItemEntry>(entry)->Count + added);
            last = entry;
        }
        return last;
    }

    InventoryResult MoveUnits(World& world, EntityId entry, EntityId into, std::uint32_t units,
                              const ItemDefinition& item, const ItemContainer& container)
    {
        const ItemEntry held = *std::as_const(world).TryGet<ItemEntry>(entry);
        const bool whole = units >= held.Count;
        const StackFill fill = FillOpenStacks(world, into, held.Definition, item, units, entry);
        if (!HasRoom(world, container, into, fill.Remaining > 0 ? 1 : 0))
            return Refused(InventoryRefusal::ContainerFull);

        EntityId moved = ApplyFills(world, fill);
        const std::uint32_t left = held.Count - (whole ? units - fill.Remaining : units);
        if (left != held.Count)
            SetItemCount(world, entry, left);
        if (whole && fill.Remaining > 0)
        {
            HoldRecord(world, entry, into);
            moved = entry;
        }
        else if (fill.Remaining > 0)
            moved = CreateEntry(world, into, held.Definition, item, fill.Remaining);
        return { .Refusal = InventoryRefusal::None, .Entry = moved };
    }
}

InventoryResult Inventory::Give(EntityId container, ItemId definition, std::uint32_t count)
{
    World& world = *Entities;
    if (!IsSimulationAuthority(world))
        return Refused(InventoryRefusal::NotAuthority);
    const ItemDefinition* item = FindItemDefinition(world, definition);
    const ItemContainer* holder = ContainerOf(world, container);
    if (item == nullptr || holder == nullptr || count == 0)
        return Refused(InventoryRefusal::Missing);
    if (const InventoryRefusal refusal = Admission(world, EntityId{}, container, *holder, *item);
        refusal != InventoryRefusal::None)
        return Refused(refusal);

    const StackFill fill = FillOpenStacks(world, container, definition, *item, count, EntityId{});
    const std::uint64_t newEntries = (std::uint64_t{ fill.Remaining } + item->StackMax - 1) / item->StackMax;
    if (newEntries > RoomOf(*holder) || !HasRoom(world, *holder, container, static_cast<std::size_t>(newEntries)))
        return Refused(InventoryRefusal::ContainerFull);

    InventoryResult result{ .Refusal = InventoryRefusal::None, .Entry = ApplyFills(world, fill) };
    for (std::uint32_t remaining = fill.Remaining; remaining > 0;)
    {
        const std::uint32_t units = std::min(remaining, item->StackMax);
        result.Entry = CreateEntry(world, container, definition, *item, units);
        remaining -= units;
    }
    return result;
}

InventoryResult Inventory::Take(EntityId pickup, EntityId into)
{
    World& world = *Entities;
    if (!IsSimulationAuthority(world))
        return Refused(InventoryRefusal::NotAuthority);
    const ItemPickup* lying = world.IsAlive(pickup) ? world.TryGet<ItemPickup>(pickup) : nullptr;
    if (lying == nullptr)
        return Refused(InventoryRefusal::Missing);

    if (const std::vector<EntityId> entries = ItemsIn(world, pickup); !entries.empty())
    {
        const EntityId entry = entries.front();
        const ItemContainer* holder = ContainerOf(world, into);
        const ItemDefinition* item = FindItemDefinition(world, EntryOf(world, entry)->Definition);
        if (holder == nullptr || item == nullptr)
            return Refused(InventoryRefusal::Missing);
        if (const InventoryRefusal refusal = Admission(world, entry, into, *holder, *item);
            refusal != InventoryRefusal::None)
            return Refused(refusal);
        if (!AdmitsMove(world, entry))
            return Refused(InventoryRefusal::Withheld);
        const InventoryResult moved = MoveUnits(world, entry, into, EntryOf(world, entry)->Count, *item, *holder);
        if (moved.Refusal != InventoryRefusal::None)
            return moved;
        if (ItemsIn(world, pickup).empty())
            world.DestroyEntity(pickup);
        TellMoved(world, entry, EntityId{});
        return moved;
    }

    const ItemId definition = lying->Definition;
    const ItemDefinition* item = FindItemDefinition(world, definition);
    if (item == nullptr || !world.IsAlive(into))
        return Refused(InventoryRefusal::Missing);
    const std::uint32_t units = lying->Count;
    InventoryResult result;
    if (item->Keep)
    {
        result = Give(into, definition, units);
        if (result.Refusal != InventoryRefusal::None)
            return result;
    }
    for (const EffectId effect : item->TakeEffects)
        (void)ApplyEffect(world, into, effect, Now, {}, static_cast<float>(units));
    world.DestroyEntity(pickup);
    return result;
}

InventoryResult Inventory::Transfer(EntityId entry, EntityId into, std::uint32_t count, ItemChangeClass change)
{
    World& world = *Entities;
    if (!IsSimulationAuthority(world))
        return Refused(InventoryRefusal::NotAuthority);
    const ItemEntry* held = EntryOf(world, entry);
    const ItemContainer* holder = ContainerOf(world, into);
    const ItemDefinition* item = held != nullptr ? FindItemDefinition(world, held->Definition) : nullptr;
    if (held == nullptr || holder == nullptr || item == nullptr || held->Count == 0 || !Contained(world, entry))
        return Refused(InventoryRefusal::Missing);
    if (const InventoryRefusal refusal = Admission(world, entry, into, *holder, *item); refusal != InventoryRefusal::None)
        return Refused(refusal);
    if (change == ItemChangeClass::Requested && !AdmitsMove(world, entry))
        return Refused(InventoryRefusal::Withheld);
    const EntityId formerHolder = ItemHolder(world, entry);
    const InventoryResult moved =
        MoveUnits(world, entry, into, count == 0 ? held->Count : std::min(count, held->Count), *item, *holder);
    if (moved.Refusal == InventoryRefusal::None)
        TellMoved(world, entry, formerHolder);
    return moved;
}

InventoryResult Inventory::Drop(EntityId entry, const Transform3f& at, StoragePartitionId partition,
                                ItemChangeClass change)
{
    World& world = *Entities;
    if (!IsSimulationAuthority(world))
        return Refused(InventoryRefusal::NotAuthority);
    const ItemEntry* held = EntryOf(world, entry);
    if (held == nullptr || held->Count == 0 || !Contained(world, entry))
        return Refused(InventoryRefusal::Missing);
    if (change == ItemChangeClass::Requested && !AdmitsMove(world, entry))
        return Refused(InventoryRefusal::Withheld);
    const EntityId formerHolder = ItemHolder(world, entry);

    const EntityId pickup = world.CreateEntity(partition);
    world.AddComponent<LocalTransform>(pickup, LocalTransform{ at });
    world.AddComponent<ItemPickup>(pickup, ItemPickup{ .Definition = {}, .Count = 0 });
    HoldRecord(world, entry, pickup);
    TellMoved(world, entry, formerHolder);
    return { .Refusal = InventoryRefusal::None, .Entry = pickup };
}

InventoryResult Inventory::Use(EntityId user, EntityId entry)
{
    World& world = *Entities;
    if (!IsSimulationAuthority(world))
        return Refused(InventoryRefusal::NotAuthority);
    const ItemEntry* held = EntryOf(world, entry);
    const ItemDefinition* item = held != nullptr ? FindItemDefinition(world, held->Definition) : nullptr;
    const AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
    const AbilityGrantEntry* use = item != nullptr && entries != nullptr ? entries->Find(item->UseEntry) : nullptr;
    AbilityRequestQueue* queue = world.TryGetResource<AbilityRequestQueue>();
    if (held == nullptr || held->Count == 0 || use == nullptr || queue == nullptr)
        return Refused(InventoryRefusal::Missing);
    if (!MayUseItem(world, user, entry))
        return Refused(InventoryRefusal::NotPermitted);
    queue->Push({ .Kind = AbilityRequestKind::ActivateEntry, .Actor = user, .Ability = use->Ability,
                  .Instigator = user, .Cause = {}, .Source = entry, .Entry = item->UseEntry });
    return { .Refusal = InventoryRefusal::None, .Entry = entry };
}

const ItemDefinition* FindItemDefinition(const World& world, ItemId item)
{
    const ItemRegistry* items = world.TryGetResource<ItemRegistry>();
    return items != nullptr ? items->Get(item) : nullptr;
}

std::vector<EntityId> ItemsIn(const World& world, EntityId container)
{
    std::vector<EntityId> entries;
    const HeldRecordIndex* index = IndexOf(world);
    if (index == nullptr || !container.IsValid() || !world.IsRegistered<ItemEntry>())
        return entries;
    for (const EntityId record : index->RecordsOf(container))
        if (world.HasComponent<ItemEntry>(record))
            entries.push_back(record);
    return entries;
}

std::vector<EntityId> HeldEntries(const World& world, EntityId holder)
{
    std::vector<EntityId> held;
    const auto walk = [&](const auto& self, EntityId container, int depth) -> void {
        if (depth >= kHeldDeepestNesting)
            return;
        for (const EntityId entry : ItemsIn(world, container))
        {
            held.push_back(entry);
            self(self, entry, depth + 1);
        }
    };
    walk(walk, holder, 0);
    return held;
}

EntityId ItemHolder(const World& world, EntityId item)
{
    EntityId at = item;
    for (int depth = 0; depth <= kHeldDeepestNesting && EntryOf(world, at) != nullptr; ++depth)
        at = HolderOf(world, at);
    return EntryOf(world, at) == nullptr ? at : EntityId{};
}

bool MayUseItem(const World& world, EntityId actor, EntityId item)
{
    return world.IsAlive(actor) && ItemHolder(world, item) == actor;
}

bool ItemIsWithin(const World& world, EntityId inner, EntityId outer)
{
    const HeldRecordIndex* index = IndexOf(world);
    return index != nullptr ? index->IsWithin(inner, outer) : inner.IsValid() && inner == outer;
}

bool ItemStackHolds(const ItemDefinition& item, std::uint32_t count)
{
    return count >= 1 && count <= std::max<std::uint32_t>(item.StackMax, 1);
}

void SetItemCount(World& world, EntityId entry, std::uint32_t count, EntityId spender)
{
    ItemEntry* item = world.IsAlive(entry) ? world.TryGet<ItemEntry>(entry) : nullptr;
    if (item == nullptr || item->Count == count)
        return;
    item->Count = count;
    NoteHeldRecordChanged(world, entry);
    if (count == 0)
        NoteDepleted(world, entry, spender);
}

bool ItemCarries(const World& world, const ItemDefinition& item, GameplayTagId tag)
{
    if (!tag.IsValid())
        return true;
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    return tags != nullptr
        && std::ranges::any_of(item.Tags, [&](GameplayTagId held) { return tags->IsDescendantOf(held, tag); });
}
