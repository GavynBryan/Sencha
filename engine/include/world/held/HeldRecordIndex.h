#pragma once

#include <core/identity/Id.h>
#include <ecs/EntityId.h>
#include <ecs/PartitionFollowers.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

// Deeper than any holder chain content builds: where a walk up holders stops at a cycle.
inline constexpr int kHeldDeepestNesting = 64;

// Which records each holder holds, in order, and a revision per holder that
// moves when anything beneath it changes. A World resource kept by Held's hooks
// and by its holders' identity hooks; docs/gameplay/held-records.md.
class HeldRecordIndex
{
public:
    // Each Track is matched by one Untrack: a replaced Held tracks the new
    // holder before the old value is untracked.
    void Track(EntityId record, EntityId holder, PersistentEntityId awaited, std::uint16_t position);
    void Untrack(EntityId record);
    void HolderArrived(PersistentEntityId id, EntityId holder);
    void HolderDeparted(EntityId holder);
    void NoteChanged(EntityId record);

    [[nodiscard]] std::span<const EntityId> RecordsOf(EntityId holder) const;
    [[nodiscard]] bool Lists(EntityId record) const { return Listings.contains(record); }
    // Invalid for a record of the world and for one whose holder has not arrived.
    [[nodiscard]] EntityId HolderOf(EntityId record) const;
    [[nodiscard]] PersistentEntityId AwaitedHolderOf(EntityId record) const;
    [[nodiscard]] std::uint32_t Revision(EntityId holder) const;
    [[nodiscard]] std::vector<EntityId> AllRecords() const;
    [[nodiscard]] std::size_t Count() const { return Listings.size(); }

    // One past the holder's last record; the last position once that is taken,
    // where records keep the order they were listed in.
    [[nodiscard]] std::uint16_t NextPosition(EntityId holder);
    [[nodiscard]] std::uint64_t PositionsExhausted() const { return Exhausted; }

    [[nodiscard]] std::vector<EntityId> TakeUnsettled();
    [[nodiscard]] std::vector<EntityId> TakeDeparted();
    void ForgetHolder(EntityId holder);

    PartitionFollowerId Follower;

private:
    struct Listing
    {
        EntityId Holder;
        PersistentEntityId Awaited;
        std::uint16_t Position = 0;
        std::uint32_t Tracks = 0;
    };

    void Insert(std::vector<EntityId>& records, EntityId record, std::uint16_t position) const;
    void Remove(EntityId record, const Listing& listing);
    void BumpFrom(EntityId holder);

    std::unordered_map<EntityId, std::vector<EntityId>, EntityIdHash> ByHolder;
    std::unordered_map<PersistentEntityId, std::vector<EntityId>> Awaiting;
    std::unordered_map<EntityId, Listing, EntityIdHash> Listings;
    std::unordered_map<EntityId, std::uint32_t, EntityIdHash> Revisions;
    std::vector<EntityId> Unsettled;
    std::vector<EntityId> Departed;
    std::uint64_t Exhausted = 0;
};
