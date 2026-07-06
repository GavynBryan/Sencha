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
        // One resolved config for both calls: resolving only the demand call
        // would demand zones past the base hop count that the ranks BFS never
        // reaches, and rank-less zones sort last in load ordering.
        const WorldPartitionStreamingConfig resolved =
            ResolveRegionStreamingConfig(Manifest_, Focus_, Config_);
        ranks = ComputeZoneHopRanks(Manifest_, Index_, Focus_, resolved.HopCount, WorldTags_);
        demand = ComputeZoneDemand(Manifest_, Index_, Focus_, Pins_, resolved,
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

    // Load: desired zones neither loaded nor in flight, closest and
    // highest-priority first into the single task queue. Pinned zones beyond
    // hop range have no rank and sort last among equals.
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
        // Always dormant: participation flips after attach, so a mid-frame
        // commit is invisible to every frame span and no discontinuity marks.
        loader.BeginLoad(record->Zone, std::move(recipe.Build), std::move(recipe.Finalize),
                         ZoneParticipation{}, std::move(recipe.Preload));
        Issued_.push_back(record->Zone);
    }

    // Participation: loaded desired zones converge on their desired flags (the
    // focus promotes to full; the previous focus demotes to dormant here in
    // the same update, through its now-dormant neighbor demand).
    for (const ZoneDemandRecord& record : demand)
    {
        if (!zones.IsZoneLoaded(record.Zone))
            continue;
        if (!SameParticipation(zones.GetParticipation(record.Zone), record.Desired))
            zones.SetParticipation(record.Zone, record.Desired);
    }

    // Cancel undemanded in-flight loads. A false return means the build is
    // mid-flight on the task thread: retried next update, reported Lingering
    // meanwhile. Attached or cancelled entries leave the issued list.
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

    // Linger and destroy: loaded, undesired, non-focus manifest zones (id
    // order for determinism) accumulate linger time, sit dormant meanwhile,
    // and are destroyed at or past the linger budget. Re-entering the desired
    // set resets the clock (handled by the demand check dropping the state).
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
            // Destruction rides the drain (never mid-frame); the zone stays
            // resident and reported until the commit runs, and must not be
            // re-requested meanwhile.
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

    // Records: the demand set plus lingering zones (including uncancellable
    // in-flight loads no longer demanded), ascending zone id.
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
