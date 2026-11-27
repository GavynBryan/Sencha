#include <world/held/HeldRecordIndex.h>

#include <algorithm>
#include <limits>
#include <utility>

void HeldRecordIndex::Track(EntityId record, EntityId holder, PersistentEntityId awaited, std::uint16_t position)
{
    auto [found, inserted] = Listings.try_emplace(record);
    Listing& listing = found->second;
    if (!inserted)
        Remove(record, listing);
    listing.Holder = awaited.IsValid() ? EntityId{} : holder;
    listing.Awaited = awaited;
    listing.Position = position;
    ++listing.Tracks;
    if (awaited.IsValid())
        Insert(Awaiting[awaited], record, position);
    else
    {
        Insert(ByHolder[listing.Holder], record, position);
        BumpFrom(listing.Holder);
    }
    Unsettled.push_back(record);
}

void HeldRecordIndex::Untrack(EntityId record)
{
    const auto found = Listings.find(record);
    if (found == Listings.end() || --found->second.Tracks > 0)
        return;
    Remove(record, found->second);
    Listings.erase(found);
}

void HeldRecordIndex::HolderArrived(PersistentEntityId id, EntityId holder)
{
    const auto found = Awaiting.find(id);
    if (found == Awaiting.end())
        return;
    const std::vector<EntityId> arrived = std::move(found->second);
    Awaiting.erase(found);
    std::vector<EntityId>& records = ByHolder[holder];
    for (const EntityId record : arrived)
    {
        Listing& listing = Listings.at(record);
        listing.Holder = holder;
        listing.Awaited = {};
        Insert(records, record, listing.Position);
        Unsettled.push_back(record);
    }
    BumpFrom(holder);
}

void HeldRecordIndex::HolderDeparted(EntityId holder)
{
    if (ByHolder.contains(holder) || Revisions.contains(holder))
        Departed.push_back(holder);
}

void HeldRecordIndex::NoteChanged(EntityId record)
{
    const auto found = Listings.find(record);
    if (found != Listings.end() && !found->second.Awaited.IsValid())
        BumpFrom(found->second.Holder);
}

std::span<const EntityId> HeldRecordIndex::RecordsOf(EntityId holder) const
{
    const auto found = ByHolder.find(holder);
    return found != ByHolder.end() ? std::span<const EntityId>(found->second) : std::span<const EntityId>{};
}

EntityId HeldRecordIndex::HolderOf(EntityId record) const
{
    const auto found = Listings.find(record);
    return found != Listings.end() ? found->second.Holder : EntityId{};
}

PersistentEntityId HeldRecordIndex::AwaitedHolderOf(EntityId record) const
{
    const auto found = Listings.find(record);
    return found != Listings.end() ? found->second.Awaited : PersistentEntityId{};
}

std::uint32_t HeldRecordIndex::Revision(EntityId holder) const
{
    const auto found = Revisions.find(holder);
    return found != Revisions.end() ? found->second : 0u;
}

std::vector<EntityId> HeldRecordIndex::AllRecords() const
{
    std::vector<EntityId> records;
    records.reserve(Listings.size());
    for (const auto& [record, listing] : Listings)
        records.push_back(record);
    std::ranges::sort(records);
    return records;
}

std::uint16_t HeldRecordIndex::NextPosition(EntityId holder)
{
    constexpr std::uint16_t kLast = std::numeric_limits<std::uint16_t>::max();
    const std::span<const EntityId> records = RecordsOf(holder);
    if (records.empty())
        return 0;
    const std::uint16_t last = Listings.at(records.back()).Position;
    if (last < kLast)
        return static_cast<std::uint16_t>(last + 1);
    ++Exhausted;
    return kLast;
}

std::vector<EntityId> HeldRecordIndex::TakeUnsettled()
{
    return std::exchange(Unsettled, {});
}

std::vector<EntityId> HeldRecordIndex::TakeDeparted()
{
    return std::exchange(Departed, {});
}

void HeldRecordIndex::ForgetHolder(EntityId holder)
{
    if (!ByHolder.contains(holder))
        Revisions.erase(holder);
}

void HeldRecordIndex::Insert(std::vector<EntityId>& records, EntityId record, std::uint16_t position) const
{
    const auto at = std::ranges::upper_bound(records, position, {},
                                             [&](EntityId listed) { return Listings.at(listed).Position; });
    records.insert(at, record);
}

void HeldRecordIndex::Remove(EntityId record, const Listing& listing)
{
    if (listing.Awaited.IsValid())
    {
        if (const auto found = Awaiting.find(listing.Awaited); found != Awaiting.end())
        {
            std::erase(found->second, record);
            if (found->second.empty())
                Awaiting.erase(found);
        }
        return;
    }
    if (const auto found = ByHolder.find(listing.Holder); found != ByHolder.end())
    {
        std::erase(found->second, record);
        if (found->second.empty())
            ByHolder.erase(found);
    }
    BumpFrom(listing.Holder);
}

void HeldRecordIndex::BumpFrom(EntityId holder)
{
    EntityId at = holder;
    for (int depth = 0; depth < kHeldDeepestNesting; ++depth)
    {
        ++Revisions[at];
        if (!at.IsValid())
            return;
        const auto above = Listings.find(at);
        if (above == Listings.end() || above->second.Awaited.IsValid())
            return;
        at = above->second.Holder;
    }
}
