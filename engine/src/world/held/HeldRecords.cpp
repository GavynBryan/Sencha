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
#include <zone/ZoneStateMemory.h>
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
            if (Held* relation = world.TryGet<Held>(inner); relation != nullptr && identity.IsValid())
                relation->Holder = PersistableRef{ .Entity = record, .Target = identity };
            LeaveAuthoredSelf(world, index, inner, leaving, depth + 1);
        }
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
        StoragePartitionId partition = PersistentStoragePartition;
        if (holder.IsValid())
        {
            const PersistableRef ref = MakePersistableRef(world, holder);
            if (Held* held = world.TryGet<Held>(record); held != nullptr && held->Holder != ref)
                held->Holder = ref;
            AnchorTo(world, record, holder);
            partition = world.GetEntityPartition(holder);
        }
        else
            AnchorTo(world, record, std::nullopt);
        if (world.GetEntityPartition(record) != partition)
            (void)world.MoveEntityToPartition(record, partition);
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
                     .Position = position.value_or(index.NextPosition(holder)) };
    if (world.HasComponent<Held>(record))
        (void)world.ReplaceComponent<Held>(record, held);
    else
        world.AddComponent<Held>(record, held);
    Settle(world, index, record);
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
            if (world.IsAlive(holder))
                continue;
            if (authority)
            {
                const std::span<const EntityId> held = index->RecordsOf(holder);
                records.assign(held.begin(), held.end());
                for (const EntityId record : records)
                    if (world.IsAlive(record))
                        world.DestroyEntity(record);
            }
            index->ForgetHolder(holder);
        }
        if (authority)
            for (const EntityId record : unsettled)
                Settle(world, *index, record);
    }
}

void ParkHeldRecords(World& world, ZoneId zone, StoragePartitionId partition)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !IsSimulationAuthority(world))
        return;
    const ZoneStateStore* memory = world.TryGetResource<ZoneStateStore>();
    std::vector<EntityId> records;
    for (const EntityId holder : IdentifiedEntitiesIn(world, partition))
    {
        const PersistentEntityId id = world.TryGet<PersistentIdComponent>(holder)->Id;
        const std::span<const EntityId> held = index->RecordsOf(holder);
        if (held.empty() || !IsAuthoredPersistentEntityId(id)
            || (memory != nullptr && memory->ForgettingOf(zone, id) == ZoneStateForgetting::AtDetach))
            continue;
        records.assign(held.begin(), held.end());
        for (const EntityId record : records)
        {
            if (HasAuthoredPersistentId(world, record))
                continue;
            index->Await(record, id);
            (void)world.MoveEntityToPartition(record, PersistentStoragePartition);
        }
    }
}

void DestroyAwaitingHeldRecords(World& world)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr || !IsSimulationAuthority(world))
        return;
    for (const EntityId record : index->AwaitingRecords())
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
