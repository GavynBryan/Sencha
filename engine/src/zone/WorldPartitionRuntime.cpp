#include <zone/WorldPartitionRuntime.h>

#include <zone/WorldPartitionIds.h>
#include <zone/WorldPartitionValidation.h>

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

namespace
{
bool SameParticipation(const ZoneParticipation& a, const ZoneParticipation& b)
{
    return a.Visible == b.Visible && a.Physics == b.Physics && a.Logic == b.Logic
        && a.Audio == b.Audio;
}
} // namespace

WorldPartitionRuntime::WorldPartitionRuntime(ZoneLoadRecipeFn recipe,
                                             WorldPartitionStreamingConfig config)
    : Recipe_(std::move(recipe))
    , Config_(config)
{
}

bool WorldPartitionRuntime::LoadManifest(WorldPartitionManifest manifest, std::string* error)
{
    for (const ZoneHeader& zone : manifest.Zones)
    {
        if (zone.CookedSceneRef.empty())
        {
            if (error != nullptr)
                *error = "zone " + ZoneIdToString(zone.Id) + " ('" + zone.Name
                    + "') has no cooked scene; cook the world first";
            return false;
        }
    }

    WorldPartitionIndex index = WorldPartitionIndex::Build(manifest);
    for (const ContentRiskRecord& record : ValidateWorldPartitionManifest(manifest, index))
    {
        if (record.Severity != ContentRiskSeverity::Error)
            continue;
        if (error != nullptr)
            *error = record.RuleId + ": " + record.Message;
        return false;
    }

    Manifest_ = std::move(manifest);
    Index_ = std::move(index);
    HasManifest_ = true;
    Focus_ = ZoneId{};
    HasFocusPosition_ = false;
    Pins_.clear();

    // Reloading a manifest invalidates every outstanding lease without allowing
    // an old token to alias a newly allocated slot in the same runtime object.
    FreeLeaseSlots_.clear();
    for (uint32_t i = 0; i < LeaseSlots_.size(); ++i)
    {
        ParticipationLeaseSlot& slot = LeaseSlots_[i];
        slot.Alive = false;
        ++slot.Generation;
        if (slot.Generation == 0)
            ++slot.Generation;
        slot.Zone = ZoneId{};
        slot.Minimum = ZoneParticipation{};
        FreeLeaseSlots_.push_back(i);
    }
    ActiveLeaseCount_ = 0;

    PendingDestroys_.clear();
    Issued_.clear();
    Lingering_.clear();
    Records_.clear();
    return true;
}

const WorldPartitionManifest& WorldPartitionRuntime::Manifest() const
{
    assert(HasManifest_ && "Manifest(): no manifest loaded");
    return Manifest_;
}

const ZoneHeader* WorldPartitionRuntime::FindHeader(ZoneId zone) const
{
    for (const ZoneHeader& header : Manifest_.Zones)
        if (header.Id == zone)
            return &header;
    return nullptr;
}

void WorldPartitionRuntime::SetFocus(Vec3d position)
{
    if (!HasManifest_)
        return;
    Focus_ = ResolveFocusZone(Manifest_, position, Focus_);
    FocusPosition_ = position;
    HasFocusPosition_ = true;
}

void WorldPartitionRuntime::SetFocus(ZoneId zone)
{
    assert(HasManifest_ && FindHeader(zone) != nullptr && "SetFocus: unknown zone");
    Focus_ = zone;
    if (const ZoneHeader* header = FindHeader(zone); header != nullptr
        && header->Bounds.IsValid())
    {
        FocusPosition_ = header->Bounds.Center();
        HasFocusPosition_ = true;
    }
}

void WorldPartitionRuntime::PinZone(ZoneId zone, ZoneParticipation minimum)
{
    for (ZonePin& pin : Pins_)
    {
        if (pin.Zone != zone)
            continue;
        pin.Minimum = minimum;
        return;
    }
    Pins_.push_back(ZonePin{ zone, minimum });
}

void WorldPartitionRuntime::UnpinZone(ZoneId zone)
{
    std::erase_if(Pins_, [&](const ZonePin& pin) { return pin.Zone == zone; });
}

ParticipationLeaseId WorldPartitionRuntime::AcquireParticipationLease(
    ZoneId zone,
    ZoneParticipation minimum)
{
    if (!HasManifest_ || !zone.IsValid() || FindHeader(zone) == nullptr)
    {
        assert(false && "AcquireParticipationLease: zone must exist in the loaded manifest");
        return ParticipationLeaseId{};
    }

    uint32_t index;
    if (!FreeLeaseSlots_.empty())
    {
        index = FreeLeaseSlots_.back();
        FreeLeaseSlots_.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(LeaseSlots_.size());
        LeaseSlots_.push_back(ParticipationLeaseSlot{});
    }

    ParticipationLeaseSlot& slot = LeaseSlots_[index];
    slot.Alive = true;
    slot.Zone = zone;
    slot.Minimum = minimum;
    ++ActiveLeaseCount_;
    return ParticipationLeaseId{ index, slot.Generation };
}

bool WorldPartitionRuntime::ReleaseParticipationLease(ParticipationLeaseId lease)
{
    if (!IsParticipationLeaseValid(lease))
        return false;

    ParticipationLeaseSlot& slot = LeaseSlots_[lease.Index];
    slot.Alive = false;
    ++slot.Generation;
    if (slot.Generation == 0)
        ++slot.Generation;
    slot.Zone = ZoneId{};
    slot.Minimum = ZoneParticipation{};
    FreeLeaseSlots_.push_back(lease.Index);
    --ActiveLeaseCount_;
    return true;
}

bool WorldPartitionRuntime::IsParticipationLeaseValid(ParticipationLeaseId lease) const
{
    return lease.IsValid() && lease.Index < LeaseSlots_.size()
        && LeaseSlots_[lease.Index].Alive
        && LeaseSlots_[lease.Index].Generation == lease.Generation;
}

std::size_t WorldPartitionRuntime::InvalidateParticipationLeases(ZoneId zone)
{
    std::size_t invalidated = 0;
    for (uint32_t index = 0; index < LeaseSlots_.size(); ++index)
    {
        ParticipationLeaseSlot& slot = LeaseSlots_[index];
        if (!slot.Alive || slot.Zone != zone)
            continue;

        slot.Alive = false;
        ++slot.Generation;
        if (slot.Generation == 0)
            ++slot.Generation;
        slot.Zone = ZoneId{};
        slot.Minimum = ZoneParticipation{};
        FreeLeaseSlots_.push_back(index);
        --ActiveLeaseCount_;
        ++invalidated;
    }
    return invalidated;
}

void WorldPartitionRuntime::SetWorldTags(std::vector<std::string> tags)
{
    WorldTags_ = std::move(tags);
}

void WorldPartitionRuntime::Update(double deltaSeconds, AsyncZoneLoader& loader,
                                   ZoneRuntime& zones)
{
    std::vector<ZoneDemandRecord> demand;
    std::vector<ZoneHopRank> ranks;
    if (HasManifest_ && Focus_.IsValid())
    {
        const WorldPartitionStreamingConfig resolved =
            ResolveRegionStreamingConfig(Manifest_, Focus_, Config_);
        ranks = ComputeZoneHopRanks(Manifest_, Index_, Focus_, resolved.HopCount, WorldTags_);

        // Leases use the existing pin-shaped pure demand input, but remain
        // independently tokenized in the runtime. Duplicate entries are safe:
        // ComputeZoneDemand ORs every floor onto the same zone.
        std::vector<ZonePin> effectivePins = Pins_;
        effectivePins.reserve(Pins_.size() + ActiveLeaseCount_);
        for (const ParticipationLeaseSlot& lease : LeaseSlots_)
            if (lease.Alive)
                effectivePins.push_back(ZonePin{ lease.Zone, lease.Minimum });

        demand = ComputeZoneDemand(Manifest_, Index_, Focus_, effectivePins, resolved,
                                   HasFocusPosition_ ? &FocusPosition_ : nullptr, WorldTags_);
    }

    const auto findDemand = [&](ZoneId zone) -> const ZoneDemandRecord*
    {
        for (const ZoneDemandRecord& record : demand)
            if (record.Zone == zone)
                return &record;
        return nullptr;
    };
    const auto findRank = [&](ZoneId zone) -> const ZoneHopRank*
    {
        for (const ZoneHopRank& rank : ranks)
            if (rank.Zone == zone)
                return &rank;
        return nullptr;
    };

    std::vector<const ZoneDemandRecord*> toLoad;
    for (const ZoneDemandRecord& record : demand)
        if (!zones.IsZoneLoaded(record.Zone) && !loader.IsLoading(record.Zone))
            toLoad.push_back(&record);
    std::sort(toLoad.begin(), toLoad.end(),
              [&](const ZoneDemandRecord* a, const ZoneDemandRecord* b)
              {
                  const ZoneHopRank* rankA = findRank(a->Zone);
                  const ZoneHopRank* rankB = findRank(b->Zone);
                  const int32_t hopA = rankA ? rankA->Hop : std::numeric_limits<int32_t>::max();
                  const int32_t hopB = rankB ? rankB->Hop : std::numeric_limits<int32_t>::max();
                  if (hopA != hopB)
                      return hopA < hopB;
                  const int32_t priA =
                      rankA ? rankA->Priority : std::numeric_limits<int32_t>::min();
                  const int32_t priB =
                      rankB ? rankB->Priority : std::numeric_limits<int32_t>::min();
                  if (priA != priB)
                      return priA > priB;
                  return a->Zone.Value < b->Zone.Value;
              });
    for (const ZoneDemandRecord* record : toLoad)
    {
        const ZoneHeader* header = FindHeader(record->Zone);
        if (header == nullptr)
            continue;
        ZoneLoadRecipe recipe = Recipe_(*header);
        loader.BeginLoad(record->Zone, std::move(recipe.Build), std::move(recipe.Finalize),
                         ZoneParticipation{}, std::move(recipe.Preload));
        Issued_.push_back(record->Zone);
    }

    for (const ZoneDemandRecord& record : demand)
    {
        if (!zones.IsZoneLoaded(record.Zone))
            continue;
        if (!SameParticipation(zones.GetParticipation(record.Zone), record.Desired))
            zones.SetParticipation(record.Zone, record.Desired);
    }

    std::vector<ZoneId> stillPending;
    for (ZoneId zone : Issued_)
    {
        if (!loader.IsLoading(zone))
            continue;
        if (findDemand(zone) == nullptr && loader.CancelLoad(zone))
            continue;
        stillPending.push_back(zone);
    }
    Issued_ = std::move(stillPending);

    std::erase_if(PendingDestroys_,
                  [&](ZoneId zone) { return !zones.IsZoneLoaded(zone); });

    std::vector<const ZoneHeader*> loadedHeaders;
    for (const ZoneHeader& header : Manifest_.Zones)
        if (zones.IsZoneLoaded(header.Id))
            loadedHeaders.push_back(&header);
    std::sort(loadedHeaders.begin(), loadedHeaders.end(),
              [](const ZoneHeader* a, const ZoneHeader* b)
              { return a->Id.Value < b->Id.Value; });

    std::vector<LingerState> lingering;
    std::vector<ZoneId> lingerRecords;
    for (const ZoneHeader* header : loadedHeaders)
    {
        const ZoneId zone = header->Id;
        if (zone == Focus_ || findDemand(zone) != nullptr)
            continue;

        LingerState state{ zone, 0.0 };
        for (const LingerState& previous : Lingering_)
            if (previous.Zone == zone)
                state.Seconds = previous.Seconds;
        state.Seconds += deltaSeconds;

        if (state.Seconds >= Config_.LingerSeconds)
        {
            bool pending = false;
            for (ZoneId requested : PendingDestroys_)
                pending |= requested == zone;
            if (!pending)
            {
                (void)loader.RequestDestroy(zone);
                PendingDestroys_.push_back(zone);
            }
        }
        if (zones.GetParticipation(zone).Any())
            zones.SetParticipation(zone, ZoneParticipation{});
        lingering.push_back(state);
        lingerRecords.push_back(zone);
    }
    Lingering_ = std::move(lingering);

    Records_ = std::move(demand);
    for (ZoneId zone : lingerRecords)
    {
        ZoneDemandRecord record;
        record.Zone = zone;
        record.Sources.Lingering = true;
        Records_.push_back(record);
    }
    for (ZoneId zone : Issued_)
    {
        bool present = false;
        for (const ZoneDemandRecord& record : Records_)
            present |= record.Zone == zone;
        if (present)
            continue;
        ZoneDemandRecord record;
        record.Zone = zone;
        record.Sources.Lingering = true;
        Records_.push_back(record);
    }
    std::sort(Records_.begin(), Records_.end(),
              [](const ZoneDemandRecord& a, const ZoneDemandRecord& b)
              { return a.Zone.Value < b.Zone.Value; });
}
