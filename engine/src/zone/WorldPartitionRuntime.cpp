#include <zone/WorldPartitionRuntime.h>

#include <world/RuntimeWorld.h>
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

void AddRuntimeReason(ZoneDemandRecord& record, ZoneDemandReasonRecord reason)
{
    if (std::find(record.Reasons.begin(), record.Reasons.end(), reason)
        == record.Reasons.end())
        record.Reasons.push_back(std::move(reason));
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
    if (!manifest.Transitions.empty())
    {
        if (error != nullptr)
            *error = "legacy transitions must be migrated to cooked docks or links";
        return false;
    }
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
    // A new world is a new set of places to stand: every source's focus,
    // traversal state, and capsule described the old one.
    Sources_.clear();
    LateTraversalCount_ = 0;
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

WorldPartitionRuntime::FocusSource& WorldPartitionRuntime::SourceFor(
    FocusSourceId source)
{
    // Kept sorted by id so the merge, and everything ordered by it, does not
    // depend on the order sources were added in.
    const auto at = std::lower_bound(
        Sources_.begin(), Sources_.end(), source,
        [](const FocusSource& held, FocusSourceId id)
        { return held.Id.Value < id.Value; });
    if (at != Sources_.end() && at->Id == source)
        return *at;
    FocusSource fresh;
    fresh.Id = source;
    return *Sources_.insert(at, fresh);
}

const WorldPartitionRuntime::FocusSource* WorldPartitionRuntime::FindSource(
    FocusSourceId source) const
{
    for (const FocusSource& held : Sources_)
        if (held.Id == source)
            return &held;
    return nullptr;
}

ZoneId WorldPartitionRuntime::FocusZone(FocusSourceId source) const
{
    const FocusSource* held = FindSource(source);
    return held == nullptr ? ZoneId{} : held->Focus;
}

const DockTraversalResult& WorldPartitionRuntime::LastTraversal(
    FocusSourceId source) const
{
    static const DockTraversalResult kNone{};
    const FocusSource* held = FindSource(source);
    return held == nullptr ? kNone : held->LastTraversal;
}

void WorldPartitionRuntime::DemandForSource(FocusSourceId source,
                                            std::vector<ZoneId>& out) const
{
    out.clear();
    const FocusSource* held = FindSource(source);
    if (!HasManifest_ || held == nullptr || !held->Focus.IsValid())
        return;

    ZoneFocusSource one;
    one.Source = source;
    one.Focus = held->Focus;
    if (held->HasPosition)
        one.Position = held->Position;

    // The focus zone's own graph overrides, exactly as the merged pass resolves
    // them: a peer standing in a zone that widens its neighborhood is owed the
    // wider one.
    const WorldPartitionStreamingConfig config =
        ResolveGraphStreamingConfig(Manifest_, held->Focus, Config_);

    for (const ZoneDemandRecord& record :
         ComputeZoneDemand(Manifest_, Index_, std::span{ &one, 1 }, {}, config))
    {
        out.push_back(record.Zone);
    }
}

bool WorldPartitionRuntime::RemoveFocusSource(FocusSourceId source)
{
    const auto at = std::find_if(Sources_.begin(), Sources_.end(),
                                 [&](const FocusSource& held)
                                 { return held.Id == source; });
    if (at == Sources_.end())
        return false;
    // Nothing is torn down here. What this source alone was holding simply
    // stops being demanded, which is the same thing that happens when a player
    // walks away from a zone, and linger covers it identically.
    Sources_.erase(at);
    return true;
}

void WorldPartitionRuntime::SetFocus(FocusSourceId source, Vec3d position)
{
    if (!HasManifest_ || !source.IsValid())
        return;
    FocusSource& held = SourceFor(source);
    if (!held.Focus.IsValid())
    {
        held.Focus = ResolveFocusZone(Manifest_, position, {});
        held.Position = position;
        held.SweepPosition = position;
        held.HasPosition = true;
        held.HasPendingPosition = false;
    }
    else
    {
        held.PendingPosition = position;
        held.HasPendingPosition = true;
    }
}

void WorldPartitionRuntime::SetFocusCapsule(FocusSourceId source, float radius,
                                            float height)
{
    if (!source.IsValid())
        return;
    FocusSource& held = SourceFor(source);
    held.CapsuleRadius = std::max(0.0f, radius);
    held.CapsuleCylinderHalfHeight =
        std::max(0.0f, height * 0.5f - held.CapsuleRadius);
}

void WorldPartitionRuntime::RelocateFocus(FocusSourceId source, Vec3d position)
{
    if (!HasManifest_ || !source.IsValid())
        return;
    FocusSource& held = SourceFor(source);
    held.Focus = ResolveFocusZone(Manifest_, position, {});
    held.Position = position;
    held.SweepPosition = position;
    held.HasPosition = true;
    held.HasPendingPosition = false;
    held.SuppressedDock = {};
    held.LastTraversal = {};
    held.Grace = {};
}

void WorldPartitionRuntime::SetFocus(FocusSourceId source, ZoneId zone)
{
    assert(HasManifest_ && FindHeader(zone) != nullptr && "SetFocus: unknown zone");
    if (!source.IsValid())
        return;
    FocusSource& held = SourceFor(source);
    held.Focus = zone;
    held.SuppressedDock = {};
    held.LastTraversal = {};
    held.Grace = {};
    held.HasPendingPosition = false;
    if (const ZoneHeader* header = FindHeader(zone); header != nullptr
        && header->Bounds.IsValid())
    {
        held.Position = header->Bounds.Center();
        held.SweepPosition = held.Position;
        held.HasPosition = true;
    }
}

std::span<const DockEndpoint> WorldPartitionRuntime::DocksFrom(ZoneId zone) const
{
    return Index_.DocksFrom(zone);
}

std::span<const LinkEndpoint> WorldPartitionRuntime::LinksFrom(ZoneId zone) const
{
    return Index_.LinksFrom(zone);
}

const GraphRecord* WorldPartitionRuntime::FindGraph(GraphId graph) const
{
    for (const GraphRecord& record : Manifest_.Graphs)
        if (record.Id == graph)
            return &record;
    return nullptr;
}

const ZoneHeader* WorldPartitionRuntime::FindZone(ZoneId zone) const
{
    return FindHeader(zone);
}

std::optional<ZoneId> WorldPartitionRuntime::ZoneAt(Vec3d position) const
{
    const ZoneContainmentResult result = ::ResolveZoneAt(Manifest_, position, {});
    return result.Chosen.IsValid() ? std::optional<ZoneId>{ result.Chosen } : std::nullopt;
}

ZoneContainmentResult WorldPartitionRuntime::ResolveZoneAt(
    Vec3d position, ZoneId preferred) const
{
    return ::ResolveZoneAt(Manifest_, position, preferred);
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

bool WorldPartitionRuntime::IsZoneLoadSuppressed(ZoneId zone) const
{
    for (const FailedLoad& record : FailedLoads_)
        if (record.Zone == zone)
            return true;
    return false;
}

void WorldPartitionRuntime::ReconcileFailedLoads(AsyncZoneLoader& loader)
{
    // Lift first: a zone whose cooked content changed, or which left the
    // manifest, is no longer described by the refusal that was recorded for it.
    std::erase_if(FailedLoads_, [&](const FailedLoad& record)
                  {
                      const ZoneHeader* header = FindHeader(record.Zone);
                      if (header == nullptr)
                      {
                          (void)loader.ClearFailure(record.Zone);
                          return true;
                      }
                      if (header->CookedContentHash == record.ContentHash)
                          return false;
                      (void)loader.ClearFailure(record.Zone);
                      return true;
                  });

    // Content identity is manifest policy rather than loader state, so the hash
    // is stamped here instead of inside the failure record itself.
    for (const ZoneLoadFailure& failure : loader.Failures())
    {
        if (IsZoneLoadSuppressed(failure.Zone))
            continue;
        const ZoneHeader* header = FindHeader(failure.Zone);
        FailedLoads_.push_back(FailedLoad{
            failure.Zone,
            header != nullptr ? header->CookedContentHash : 0 });
    }
}

void WorldPartitionRuntime::Update(double deltaSeconds, AsyncZoneLoader& loader,
                                   RuntimeWorld& world)
{
    ReconcileFailedLoads(loader);

    // Every source sweeps its own movement through the doors it is actually
    // walking through. Resident physics zones are gathered once: the set is a
    // fact about the world rather than about who is asking.
    std::vector<ZoneId> residentPhysicsZones;
    const bool anySweep =
        HasManifest_
        && std::any_of(Sources_.begin(), Sources_.end(),
                       [](const FocusSource& held)
                       { return held.Focus.IsValid() && held.HasPendingPosition; });
    if (anySweep)
    {
        for (const ZoneHeader& zone : Manifest_.Zones)
        {
            const RuntimeZoneRecord* resident = world.FindZone(zone.Id);
            if (resident != nullptr && resident->Participation.Physics)
                residentPhysicsZones.push_back(zone.Id);
        }
    }

    for (FocusSource& held : Sources_)
    {
        held.LastTraversal = {};
        if (!HasManifest_ || !held.Focus.IsValid() || !held.HasPendingPosition)
            continue;

        ZoneFocusState state{ held.Focus, {}, held.SuppressedDock,
                              held.SweepPosition };
        held.LastTraversal = AdvanceZoneFocus(
            state, Index_, held.PendingPosition,
            DockCrossingOptions{
                .CapsuleRadius = held.CapsuleRadius,
                .CapsuleHalfHeight = held.CapsuleCylinderHalfHeight,
                .ResidentPhysicsZones = residentPhysicsZones,
                .RequireResidentDestination = true,
            });
        if (held.LastTraversal.Status
            == DockTraversalStatus::BlockedDestinationNotReady)
        {
            ++LateTraversalCount_;
        }
        held.Focus = state.Current;
        held.SuppressedDock = state.SuppressedDock;
        held.SweepPosition = state.PreviousPosition;
        held.Position = held.LastTraversal.Status
                == DockTraversalStatus::BlockedDestinationNotReady
            ? held.LastTraversal.SafeSourcePosition : held.PendingPosition;
        held.HasPosition = true;
        held.HasPendingPosition = false;
        if (held.LastTraversal.Status == DockTraversalStatus::Crossed)
            held.Grace = { held.LastTraversal.From, 0.0 };
    }

    std::vector<ZoneDemandRecord> demand;
    std::vector<ZoneHopRank> ranks;
    std::vector<ZoneFocusSource> policySources;
    for (const FocusSource& held : Sources_)
    {
        if (!held.Focus.IsValid())
            continue;
        policySources.push_back(ZoneFocusSource{
            .Source = held.Id,
            .Focus = held.Focus,
            .Position = held.HasPosition ? std::optional<Vec3d>{ held.Position }
                                         : std::optional<Vec3d>{},
        });
    }

    if (HasManifest_ && !policySources.empty())
    {
        // Leases use the existing pin-shaped pure demand input, but remain
        // independently tokenized in the runtime. Duplicate entries are safe:
        // ComputeZoneDemand ORs every floor onto the same zone.
        std::vector<ZonePin> effectivePins = Pins_;
        effectivePins.reserve(Pins_.size() + ActiveLeaseCount_);
        for (const ParticipationLeaseSlot& lease : LeaseSlots_)
            if (lease.Alive)
                effectivePins.push_back(ZonePin{ lease.Zone, lease.Minimum });

        // The base config, not one source's resolved one. Per-graph overrides
        // are resolved per source inside the policy, and a graph's cap bounding
        // a different graph because one player happened to be standing in it is
        // not what those overrides mean.
        demand = ComputeZoneDemand(Manifest_, Index_, policySources, effectivePins,
                                   Config_);

        // Load order wants the same nearest-first ranking the single-focus path
        // used, merged the way the demand merged: a zone is as near as the
        // nearest source that asked for it.
        for (const ZoneFocusSource& source : policySources)
        {
            const WorldPartitionStreamingConfig resolved =
                ResolveGraphStreamingConfig(Manifest_, source.Focus, Config_);
            for (const ZoneHopRank& rank :
                 ComputeZoneHopRanks(Manifest_, Index_, source.Focus,
                                     resolved.HopCount))
            {
                const auto held = std::find_if(
                    ranks.begin(), ranks.end(), [&](const ZoneHopRank& existing)
                    { return existing.Zone == rank.Zone; });
                if (held == ranks.end())
                    ranks.push_back(rank);
                else if (rank.Hop < held->Hop
                         || (rank.Hop == held->Hop && rank.Cost < held->Cost))
                    *held = rank;
            }
        }
    }

    for (FocusSource& held : Sources_)
    {
        if (!held.Grace.Zone.IsValid())
            continue;
        held.Grace.Seconds += deltaSeconds;
        if (held.Grace.Seconds > std::max(0.0, Config_.LingerSeconds))
        {
            held.Grace = {};
            continue;
        }

        ZoneDemandRecord* previous = nullptr;
        for (ZoneDemandRecord& record : demand)
            if (record.Zone == held.Grace.Zone)
                previous = &record;
        if (previous == nullptr)
        {
            ZoneDemandRecord record;
            record.Zone = held.Grace.Zone;
            record.Desired = ZoneParticipation{
                .Visible = Config_.NeighborVisible,
                .Physics = Config_.NeighborPhysics,
            };
            demand.push_back(std::move(record));
            previous = &demand.back();
        }
        AddRuntimeReason(*previous,
                         { ZoneDemandReason::TraversalGrace, held.Focus,
                           held.LastTraversal.Status
                                   == DockTraversalStatus::Crossed
                               ? held.LastTraversal.Dock.Value : 0,
                           0, {} });
    }

    std::sort(demand.begin(), demand.end(),
              [](const ZoneDemandRecord& a, const ZoneDemandRecord& b)
              { return a.Zone.Value < b.Zone.Value; });

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

    // Load: desired zones neither loaded nor in flight, closest policy rank
    // first into the single task queue. Pinned zones beyond
    // hop range have no rank and sort last among equals.
    std::vector<const ZoneDemandRecord*> toLoad;
    for (const ZoneDemandRecord& record : demand)
        if (!world.IsZoneResident(record.Zone) && !loader.IsLoading(record.Zone)
            && !IsZoneLoadSuppressed(record.Zone))
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
                  const double costA = rankA ? rankA->Cost : 0.0;
                  const double costB = rankB ? rankB->Cost : 0.0;
                  if (costA != costB)
                      return costA < costB;
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
        if (!world.IsZoneResident(record.Zone))
            continue;
        const RuntimeZoneRecord* resident = world.FindZone(record.Zone);
        if (resident != nullptr
            && !SameParticipation(resident->Participation, record.Desired))
        {
            (void)world.RequestParticipation(record.Zone, record.Desired);
        }
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
                  [&](ZoneId zone) { return !world.IsZoneResident(zone); });

    std::vector<const ZoneHeader*> loadedHeaders;
    for (const ZoneHeader& header : Manifest_.Zones)
        if (world.IsZoneResident(header.Id))
            loadedHeaders.push_back(&header);
    std::sort(loadedHeaders.begin(), loadedHeaders.end(),
              [](const ZoneHeader* a, const ZoneHeader* b)
              { return a->Id.Value < b->Id.Value; });

    std::vector<LingerState> lingering;
    std::vector<ZoneId> lingerRecords;
    for (const ZoneHeader* header : loadedHeaders)
    {
        const ZoneId zone = header->Id;
        // A zone somebody is standing in never lingers, whoever that is.
        //
        // Belt and braces: every source's focus is in the demand set and immune
        // from eviction, so findDemand already catches this and no test can
        // reach the difference. Kept, and generalized rather than left naming
        // only the primary source, because it states the invariant at the point
        // that depends on it -- and because the version that named one source
        // would read as a rule about that source.
        const bool focusedBySomebody = std::any_of(
            Sources_.begin(), Sources_.end(),
            [&](const FocusSource& held) { return held.Focus == zone; });
        if (focusedBySomebody || findDemand(zone) != nullptr)
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
        const RuntimeZoneRecord* resident = world.FindZone(zone);
        if (resident != nullptr && resident->Participation.Any())
            (void)world.RequestParticipation(zone, ZoneParticipation{});
        lingering.push_back(state);
        lingerRecords.push_back(zone);
    }
    Lingering_ = std::move(lingering);

    Records_ = std::move(demand);
    for (ZoneId zone : lingerRecords)
    {
        ZoneDemandRecord record;
        record.Zone = zone;
        // Attributed to the primary source: linger is a fact about the zone
        // rather than about who left it, and the field exists to name a source
        // zone rather than a source.
        AddRuntimeReason(record,
                         { ZoneDemandReason::Linger, FocusZone(kPrimaryFocusSource),
                           0, 0, {} });
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
        AddRuntimeReason(record,
                         { ZoneDemandReason::Linger, FocusZone(kPrimaryFocusSource),
                           0, 0, {} });
        Records_.push_back(record);
    }
    std::sort(Records_.begin(), Records_.end(),
              [](const ZoneDemandRecord& a, const ZoneDemandRecord& b)
              { return a.Zone.Value < b.Zone.Value; });
}
