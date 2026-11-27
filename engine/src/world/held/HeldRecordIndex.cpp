#include <world/held/HeldRecordIndex.h>

#include <algorithm>
#include <iterator>
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
    listing.Scope = {};
    listing.SetAside = false;
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
        listing.Scope = {};
        Insert(records, record, listing.Position);
        Unsettled.push_back(record);
    }
    BumpFrom(holder);
}

void HeldRecordIndex::HolderDeparted(EntityId holder)
{
    if (ByHolder.contains(holder))
        Departed.push_back(holder);
}

void HeldRecordIndex::Await(EntityId record, PersistentEntityId holder, ZoneStateScope scope)
{
    const auto found = Listings.find(record);
    if (found == Listings.end() || !holder.IsValid()
        || (found->second.Awaited == holder && found->second.Scope == scope && !found->second.SetAside))
        return;
    Listing& listing = found->second;
    Remove(record, listing);
    listing.Holder = {};
    listing.Awaited = holder;
    listing.Scope = scope;
    listing.SetAside = false;
    Insert(Awaiting[holder], record, listing.Position);
    Unsettled.push_back(record);
}

void HeldRecordIndex::SetAside(ZoneStateLevelId level)
{
    if (!level.IsValid())
        return;
    std::vector<EntityId>& aside = SetAsideByLevel[level];
    for (auto it = Awaiting.begin(); it != Awaiting.end();)
    {
        std::erase_if(it->second, [&](EntityId record) {
            Listing& listing = Listings.at(record);
            if (listing.Scope.Level != level)
                return false;
            listing.SetAside = true;
            aside.push_back(record);
            return true;
        });
        it = it->second.empty() ? Awaiting.erase(it) : std::next(it);
    }
    std::ranges::sort(aside);
    if (aside.empty())
        SetAsideByLevel.erase(level);
}

void HeldRecordIndex::Resume(ZoneStateLevelId level)
{
    const auto found = SetAsideByLevel.find(level);
    if (found == SetAsideByLevel.end())
        return;
    const std::vector<EntityId> resumed = std::move(found->second);
    SetAsideByLevel.erase(found);
    for (const EntityId record : resumed)
    {
        Listing& listing = Listings.at(record);
        listing.SetAside = false;
        Insert(Awaiting[listing.Awaited], record, listing.Position);
    }
}

void HeldRecordIndex::NoteChanged(EntityId record)
{
    const auto found = Listings.find(record);
    if (found != Listings.end() && !found->second.Awaited.IsValid())
        BumpFrom(found->second.Holder);
}

void HeldRecordIndex::Key(EntityId record, HeldRecordKey key)
{
    ByKey[key].push_back(record);
}

void HeldRecordIndex::Unkey(EntityId record, HeldRecordKey key)
{
    const auto found = ByKey.find(key);
    if (found == ByKey.end())
        return;
    const auto keying = std::ranges::find(found->second, record);
    if (keying == found->second.end())
        return;
    found->second.erase(keying);
    if (found->second.empty())
        ByKey.erase(found);
}

EntityId HeldRecordIndex::FindKeyed(EntityId holder, HeldRecordKey key) const
{
    const auto found = ByKey.find(key);
    if (found == ByKey.end())
        return {};
    for (const EntityId record : found->second)
        if (const auto listing = Listings.find(record);
            listing != Listings.end() && !listing->second.Awaited.IsValid() && listing->second.Holder == holder)
            return record;
    return {};
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

ZoneStateScope HeldRecordIndex::AwaitingScopeOf(EntityId record) const
{
    const auto found = Listings.find(record);
    return found != Listings.end() && found->second.Awaited.IsValid() ? found->second.Scope : ZoneStateScope{};
}

std::vector<EntityId> HeldRecordIndex::SetAsideRecords(ZoneStateLevelId level) const
{
    const auto found = SetAsideByLevel.find(level);
    return found != SetAsideByLevel.end() ? found->second : std::vector<EntityId>{};
}

std::vector<EntityId> HeldRecordIndex::SetAsideRecords() const
{
    std::vector<EntityId> records;
    for (const auto& [level, aside] : SetAsideByLevel)
        records.insert(records.end(), aside.begin(), aside.end());
    std::ranges::sort(records);
    return records;
}

std::uint32_t HeldRecordIndex::Revision(EntityId holder) const
{
    const auto found = Revisions.find(holder);
    return found != Revisions.end() ? found->second : 0u;
}

int HeldRecordIndex::Depth(EntityId entity) const
{
    int depth = 0;
    for (EntityId at = entity; depth <= kHeldDeepestNesting && at.IsValid(); ++depth)
    {
        const auto found = Listings.find(at);
        if (found == Listings.end())
            return depth;
        at = found->second.Holder;
    }
    return depth;
}

bool HeldRecordIndex::IsWithin(EntityId inner, EntityId outer) const
{
    if (!outer.IsValid())
        return false;
    EntityId at = inner;
    for (int depth = 0; depth <= kHeldDeepestNesting && at.IsValid(); ++depth)
    {
        if (at == outer)
            return true;
        const auto found = Listings.find(at);
        if (found == Listings.end())
            return false;
        at = found->second.Holder;
    }
    return false;
}

int HeldRecordIndex::Height(EntityId entity) const
{
    int height = 0;
    std::vector<EntityId> level{ entity };
    while (height <= kHeldDeepestNesting)
    {
        std::vector<EntityId> below;
        for (const EntityId at : level)
            below.insert(below.end(), RecordsOf(at).begin(), RecordsOf(at).end());
        if (below.empty())
            return height;
        level = std::move(below);
        ++height;
    }
    return height;
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

std::vector<EntityId> HeldRecordIndex::Holders() const
{
    std::vector<EntityId> holders;
    holders.reserve(ByHolder.size());
    for (const auto& [holder, records] : ByHolder)
        if (holder.IsValid())
            holders.push_back(holder);
    std::ranges::sort(holders);
    return holders;
}

std::vector<EntityId> HeldRecordIndex::AwaitingRecords() const
{
    std::vector<PersistentEntityId> holders;
    holders.reserve(Awaiting.size());
    for (const auto& [id, waiting] : Awaiting)
        holders.push_back(id);
    std::ranges::sort(holders, {}, &PersistentEntityId::Value);
    std::vector<EntityId> records;
    for (const PersistentEntityId holder : holders)
    {
        const std::vector<EntityId>& waiting = Awaiting.at(holder);
        records.insert(records.end(), waiting.begin(), waiting.end());
    }
    return records;
}

namespace
{
    constexpr std::uint16_t kLastPosition = std::numeric_limits<std::uint16_t>::max();
}

std::uint16_t HeldRecordIndex::PositionOf(EntityId record) const
{
    const auto found = Listings.find(record);
    return found != Listings.end() ? found->second.Position : std::uint16_t{ 0 };
}

bool HeldRecordIndex::Renumber(EntityId holder)
{
    const auto found = ByHolder.find(holder);
    if (found == ByHolder.end() || found->second.size() > kLastPosition
        || Listings.at(found->second.back()).Position < kLastPosition)
        return false;
    std::uint16_t position = 0;
    for (const EntityId record : found->second)
        Listings.at(record).Position = position++;
    return true;
}

std::uint16_t HeldRecordIndex::NextPosition(EntityId holder)
{
    const std::span<const EntityId> records = RecordsOf(holder);
    if (records.empty())
        return 0;
    const std::uint16_t last = Listings.at(records.back()).Position;
    if (last < kLastPosition)
        return static_cast<std::uint16_t>(last + 1);
    ++Exhausted;
    return kLastPosition;
}

std::vector<EntityId> HeldRecordIndex::TakeUnsettled()
{
    return std::exchange(Unsettled, {});
}

std::vector<EntityId> HeldRecordIndex::TakeDeparted()
{
    return std::exchange(Departed, {});
}

void HeldRecordIndex::Insert(std::vector<EntityId>& records, EntityId record, std::uint16_t position) const
{
    const auto at = std::ranges::upper_bound(records, position, {},
                                             [&](EntityId listed) { return Listings.at(listed).Position; });
    records.insert(at, record);
}

void HeldRecordIndex::Remove(EntityId record, const Listing& listing)
{
    if (listing.SetAside)
    {
        if (const auto found = SetAsideByLevel.find(listing.Scope.Level); found != SetAsideByLevel.end())
        {
            std::erase(found->second, record);
            if (found->second.empty())
                SetAsideByLevel.erase(found);
        }
        return;
    }
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
    if (++Stamp == 0)
        ++Stamp;
    EntityId at = holder;
    for (int depth = 0; depth < kHeldDeepestNesting; ++depth)
    {
        if (ByHolder.contains(at))
            Revisions[at] = Stamp;
        else
            Revisions.erase(at);
        if (!at.IsValid())
            return;
        const auto above = Listings.find(at);
        if (above == Listings.end() || above->second.Awaited.IsValid())
            return;
        at = above->second.Holder;
    }
}
