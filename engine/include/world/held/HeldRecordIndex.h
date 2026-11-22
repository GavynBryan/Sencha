#pragma once

#include <core/identity/Id.h>
#include <ecs/ComponentTypeId.h>
#include <ecs/EntityId.h>
#include <ecs/PartitionFollowers.h>
#include <zone/ZoneStateScope.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

inline constexpr int kHeldDeepestNesting = 64;

struct HeldRecordKey
{
    ComponentTypeId Kind;
    std::uint32_t Name = 0;

    friend bool operator==(const HeldRecordKey&, const HeldRecordKey&) = default;
};

struct HeldRecordKeyHash
{
    std::size_t operator()(const HeldRecordKey& key) const noexcept
    {
        return std::hash<std::uint64_t>{}(key.Kind.Value ^ (std::uint64_t{ key.Name } * 0x9E3779B97F4A7C15ull));
    }
};

class HeldRecordIndex
{
public:
    void Track(EntityId record, EntityId holder, PersistentEntityId awaited, std::uint16_t position);
    void Untrack(EntityId record);
    void HolderArrived(PersistentEntityId id, EntityId holder);
    void HolderDeparted(EntityId holder);
    // `scope` is the zone the holder belongs to; none for a record waiting for a body.
    void Await(EntityId record, PersistentEntityId holder, ZoneStateScope scope = {});
    // A level's waiting records leave every lookup while another level is loaded.
    void SetAside(ZoneStateLevelId level);
    void Resume(ZoneStateLevelId level);
    void NoteChanged(EntityId record);
    void Key(EntityId record, HeldRecordKey key);
    void Unkey(EntityId record, HeldRecordKey key);

    [[nodiscard]] std::span<const EntityId> RecordsOf(EntityId holder) const;
    [[nodiscard]] bool Lists(EntityId record) const { return Listings.contains(record); }
    [[nodiscard]] EntityId FindKeyed(EntityId holder, HeldRecordKey key) const;
    [[nodiscard]] EntityId HolderOf(EntityId record) const;
    [[nodiscard]] PersistentEntityId AwaitedHolderOf(EntityId record) const;
    [[nodiscard]] ZoneStateScope AwaitingScopeOf(EntityId record) const;
    [[nodiscard]] std::uint32_t Revision(EntityId holder) const;
    [[nodiscard]] int Depth(EntityId entity) const;
    [[nodiscard]] int Height(EntityId entity) const;
    [[nodiscard]] bool IsWithin(EntityId inner, EntityId outer) const;
    [[nodiscard]] std::vector<EntityId> AllRecords() const;
    [[nodiscard]] std::vector<EntityId> Holders() const;
    [[nodiscard]] std::vector<EntityId> AwaitingRecords() const;
    // Set aside, in record order.
    [[nodiscard]] std::vector<EntityId> SetAsideRecords(ZoneStateLevelId level) const;
    [[nodiscard]] std::vector<EntityId> SetAsideRecords() const;
    [[nodiscard]] std::size_t Count() const { return Listings.size(); }

    [[nodiscard]] std::uint16_t NextPosition(EntityId holder);
    [[nodiscard]] std::uint16_t PositionOf(EntityId record) const;
    [[nodiscard]] bool Renumber(EntityId holder);
    [[nodiscard]] std::uint64_t PositionsExhausted() const { return Exhausted; }

    [[nodiscard]] std::vector<EntityId> TakeUnsettled();
    [[nodiscard]] std::vector<EntityId> TakeDeparted();

    PartitionFollowerId Follower;

private:
    struct Listing
    {
        EntityId Holder;
        PersistentEntityId Awaited;
        ZoneStateScope Scope;
        bool SetAside = false;
        std::uint16_t Position = 0;
        std::uint32_t Tracks = 0;
    };

    void Insert(std::vector<EntityId>& records, EntityId record, std::uint16_t position) const;
    void Remove(EntityId record, const Listing& listing);
    void BumpFrom(EntityId holder);

    std::unordered_map<EntityId, std::vector<EntityId>, EntityIdHash> ByHolder;
    std::unordered_map<PersistentEntityId, std::vector<EntityId>> Awaiting;
    std::unordered_map<ZoneStateLevelId, std::vector<EntityId>> SetAsideByLevel;
    std::unordered_map<EntityId, Listing, EntityIdHash> Listings;
    std::unordered_map<EntityId, std::uint32_t, EntityIdHash> Revisions;
    std::unordered_map<HeldRecordKey, std::vector<EntityId>, HeldRecordKeyHash> ByKey;
    std::vector<EntityId> Unsettled;
    std::vector<EntityId> Departed;
    std::uint64_t Exhausted = 0;
    std::uint32_t Stamp = 0;
};
