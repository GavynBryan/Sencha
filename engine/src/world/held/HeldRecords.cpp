#include <world/held/HeldRecords.h>

#include <ecs/PartitionFollowers.h>
#include <ecs/World.h>
#include <net/NetReplicationComponents.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>
#include <world/held/Held.h>
#include <world/held/HeldRecordIndex.h>
#include <world/identity/PersistentIdComponent.h>
#include <world/identity/PersistentIdMint.h>
#include <world/scene/SceneInstance.h>
#include <zone/KeptWithZone.h>
#include <zone/ZoneStateStore.h>

#include <vector>

namespace
{
    void FollowHolders(void*, World& world, std::span<const EntityPartitionMove> moves)
    {
        const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
        if (index == nullptr)
            return;
        std::vector<EntityId> records;
        for (const EntityPartitionMove& move : moves)
        {
            const std::span<const EntityId> held = index->RecordsOf(move.Entity);
            records.assign(held.begin(), held.end());
            for (const EntityId record : records)
                if (world.IsAlive(record) && world.GetEntityPartition(record) != move.Current)
                    (void)world.MoveEntityToPartition(record, move.Current);
        }
    }

    void AnchorTo(World& world, EntityId record, std::optional<EntityId> anchor)
    {
        if (!world.IsRegistered<NetRelevanceAnchor>())
            return;
        NetRelevanceAnchor* current = world.TryGet<NetRelevanceAnchor>(record);
        if (!anchor.has_value())
        {
            if (current != nullptr)
                world.RemoveComponent<NetRelevanceAnchor>(record);
            return;
        }
        if (current == nullptr)
            world.AddComponent<NetRelevanceAnchor>(record, NetRelevanceAnchor{ .Anchor = *anchor });
        else
            current->Anchor = *anchor;
    }

    enum class Leaving : std::uint8_t
    {
        Identity,
        IdentityAndGroup,
    };

    void LeaveAuthoredSelf(World& world, const HeldRecordIndex& index, EntityId record, Leaving leaving, int depth)
    {
        if (depth >= kHeldDeepestNesting)
            return;
        if (leaving == Leaving::IdentityAndGroup && world.IsRegistered<SceneInstance>()
            && world.HasComponent<SceneInstance>(record))
            world.RemoveComponent<SceneInstance>(record);
        PersistentEntityId identity;
        if (HasAuthoredPersistentId(world, record))
        {
            identity = MintRuntimePersistentId(world);
            if (identity.IsValid())
                (void)world.ReplaceComponent<PersistentIdComponent>(record, PersistentIdComponent{ identity });
        }
        const std::span<const EntityId> listed = index.RecordsOf(record);
        const std::vector<EntityId> held(listed.begin(), listed.end());
        for (const EntityId inner : held)
        {
            if (identity.IsValid())
                NameHeldHolder(world, inner);
            LeaveAuthoredSelf(world, index, inner, leaving, depth + 1);
        }
    }

    StoragePartitionId SettledPartition(const World& world, EntityId holder)
    {
        return holder.IsValid() ? world.GetEntityPartition(holder) : PersistentStoragePartition;
    }

    void Settle(World& world, const HeldRecordIndex& index, EntityId record)
    {
        if (!world.IsAlive(record) || !index.Lists(record))
            return;
        const EntityId holder = index.HolderOf(record);
        if (holder.IsValid() && !world.IsAlive(holder))
        {
            world.DestroyEntity(record);
            return;
        }
        (void)EnsurePersistentId(world, record);
        if (world.IsRegistered<NetReplicated>() && !world.HasComponent<NetReplicated>(record))
            world.AddComponent<NetReplicated>(record);
        if (index.AwaitedHolderOf(record).IsValid())
        {
            AnchorTo(world, record, EntityId{});
            return;
        }
        if (holder.IsValid())
        {
            NameHeldHolder(world, record);
            AnchorTo(world, record, holder);
        }
        else
            AnchorTo(world, record, std::nullopt);
        if (const StoragePartitionId partition = SettledPartition(world, holder);
            world.GetEntityPartition(record) != partition)
            (void)world.MoveEntityToPartition(record, partition);
    }

    std::uint16_t PositionAfterLast(World& world, HeldRecordIndex& index, EntityId holder)
    {
        if (index.Renumber(holder))
            for (const EntityId listed : index.RecordsOf(holder))
                world.TryGet<Held>(listed)->Position = index.PositionOf(listed);
        return index.NextPosition(holder);
    }
}

HeldRecordIndex& InstallHeldRecords(World& world)
{
    HeldRecordIndex& index = world.HasResource<HeldRecordIndex>() ? world.GetResource<HeldRecordIndex>()
                                                                  : world.AddResource<HeldRecordIndex>();
    if (!index.Follower.IsValid())
    {
        PartitionFollowers& followers = world.HasResource<PartitionFollowers>() ? world.GetResource<PartitionFollowers>()
                                                                                : world.AddResource<PartitionFollowers>();
        index.Follower = followers.Register(nullptr, &FollowHolders);
    }
    return index;
}

void HoldRecord(World& world, EntityId record, EntityId holder, std::optional<std::uint16_t> position)
{
    if (!IsSimulationAuthority(world) || !world.IsAlive(record) || (holder.IsValid() && !world.IsAlive(holder)))
        return;
    HeldRecordIndex& index = InstallHeldRecords(world);
    if (index.Lists(record) && (index.HolderOf(record) != holder || index.AwaitedHolderOf(record).IsValid()))
        LeaveAuthoredSelf(world, index, record, Leaving::IdentityAndGroup, 0);
    const Held held{ .Holder = MakePersistableRef(world, holder),
                     .Position = position.has_value() ? *position : PositionAfterLast(world, index, holder) };
    if (world.HasComponent<Held>(record))
        (void)world.ReplaceComponent<Held>(record, held);
    else
        world.AddComponent<Held>(record, held);
    Settle(world, index, record);
}

EntityId PlaceHeldRecord(World& world, EntityId holder, ArchetypeSignature values, std::optional<std::uint16_t> position)
{
    if (!IsSimulationAuthority(world) || !world.IsRegistered<Held>() || (holder.IsValid() && !world.IsAlive(holder)))
        return {};
    HeldRecordIndex& index = InstallHeldRecords(world);
    const Held held{ .Holder = MakePersistableRef(world, holder),
                     .Position = position.has_value() ? *position : PositionAfterLast(world, index, holder) };
    const PersistentEntityId identity =
        world.IsRegistered<PersistentIdComponent>() ? MintRuntimePersistentId(world) : PersistentEntityId{};
    const bool replicated = world.IsRegistered<NetReplicated>();
    const bool anchored = holder.IsValid() && world.IsRegistered<NetRelevanceAnchor>();
    values.set(world.GetComponentId<Held>());
    if (identity.IsValid())
        values.set(world.GetComponentId<PersistentIdComponent>());
    if (replicated)
        values.set(world.GetComponentId<NetReplicated>());
    if (anchored)
        values.set(world.GetComponentId<NetRelevanceAnchor>());

    const EntityId record = world.CreateEntityWithSignature(SettledPartition(world, holder), values);
    if (identity.IsValid())
        (void)world.InitializeComponent<PersistentIdComponent>(record, PersistentIdComponent{ identity });
    if (replicated)
        (void)world.InitializeComponent<NetReplicated>(record);
    if (anchored)
        (void)world.InitializeComponent<NetRelevanceAnchor>(record, NetRelevanceAnchor{ .Anchor = holder });
    (void)world.InitializeComponent<Held>(record, held);
    return record;
}

void NameHeldHolder(World& world, EntityId record)
{
    const HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    const EntityId holder = index != nullptr ? index->HolderOf(record) : EntityId{};
    if (!holder.IsValid())
        return;
    const PersistableRef named = MakePersistableRef(world, holder);
    if (Held* held = world.TryGet<Held>(record); held != nullptr && held->Holder != named)
        held->Holder = named;
}

void NoteHeldRecordChanged(World& world, EntityId record)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr)
        return;
    if (IsSimulationAuthority(world) && world.IsAlive(record))
        LeaveAuthoredSelf(world, *index, record, Leaving::Identity, 0);
    index->NoteChanged(record);
}

void SettleHeldRecords(World& world)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr)
        return;
    const bool authority = IsSimulationAuthority(world);
    std::vector<EntityId> records;
    for (;;)
    {
        const std::vector<EntityId> departed = index->TakeDeparted();
        const std::vector<EntityId> unsettled = index->TakeUnsettled();
        if (departed.empty() && unsettled.empty())
            return;
        for (const EntityId holder : departed)
        {
            if (world.IsAlive(holder) || !authority)
                continue;
            const std::span<const EntityId> held = index->RecordsOf(holder);
            records.assign(held.begin(), held.end());
            for (const EntityId record : records)
                if (world.IsAlive(record))
                    world.DestroyEntity(record);
        }
        if (authority)
            for (const EntityId record : unsettled)
                Settle(world, *index, record);
    }
}

void ParkHeldRecords(World& world, ZoneStateScope scope, std::span<const EntityId> holders)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !IsSimulationAuthority(world))
        return;
    const ZoneStateStore* memory = world.TryGetResource<ZoneStateStore>();
    const ZoneStateSerialization* kept = world.TryGetResource<ZoneStateSerialization>();
    const bool keepsGained = kept != nullptr && kept->Serializers != nullptr;
    std::vector<EntityId> records;
    for (const EntityId holder : holders)
    {
        const PersistentIdComponent* identity =
            world.IsAlive(holder) ? world.TryGet<PersistentIdComponent>(holder) : nullptr;
        if (identity == nullptr)
            continue;
        const PersistentEntityId id = identity->Id;
        const std::span<const EntityId> held = index->RecordsOf(holder);
        const bool authored = IsAuthoredPersistentEntityId(id);
        if (held.empty() || (!authored && !(keepsGained && IsKeptWithZone(world, holder))))
            continue;
        const ZoneStateForgetting forgotten = !authored ? ForgettingOfKept(world, holder)
            : memory != nullptr                         ? memory->ForgettingOf(scope, id)
                                                        : ZoneStateForgetting::Never;
        if (forgotten == ZoneStateForgetting::AtDetach)
            continue;
        records.assign(held.begin(), held.end());
        for (const EntityId record : records)
        {
            if (HasAuthoredPersistentId(world, record))
                continue;
            index->Await(record, id, scope);
            (void)world.MoveEntityToPartition(record, PersistentStoragePartition);
        }
    }
}

void SetAsideHeldRecords(World& world, ZoneStateLevelId level)
{
    if (HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>())
        index->SetAside(level);
}

void ResumeHeldRecords(World& world, ZoneStateLevelId level)
{
    if (HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>())
        index->Resume(level);
}

void DestroySetAsideHeldRecords(World& world, ZoneStateLevelId level)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !IsSimulationAuthority(world))
        return;
    for (const EntityId record : index->SetAsideRecords(level))
        if (world.IsAlive(record))
            world.DestroyEntity(record);
}

void DestroyHeldRecords(World& world)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !IsSimulationAuthority(world))
        return;
    for (std::size_t before = index->Count() + 1; index->Count() > 0 && index->Count() < before;)
    {
        before = index->Count();
        for (const EntityId record : index->AllRecords())
            if (world.IsAlive(record) && !HasAuthoredPersistentId(world, record))
                world.DestroyEntity(record);
    }
    (void)index->TakeDeparted();
    (void)index->TakeUnsettled();
}
