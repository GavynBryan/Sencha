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
    Focus_ = ZoneId{};
    SuppressedDock_ = {};
    LastTraversal_ = {};
    HasFocusPosition_ = false;
    HasPendingFocusPosition_ = false;
    FocusCapsuleRadius_ = 0.0f;
    FocusCapsuleCylinderHalfHeight_ = 0.0f;
    LateTraversalCount_ = 0;
    TraversalGrace_ = {};
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
    if (!Focus_.IsValid())
    {
        Focus_ = ResolveFocusZone(Manifest_, position, {});
        FocusPosition_ = position;
        DockSweepPosition_ = position;
        HasFocusPosition_ = true;
        HasPendingFocusPosition_ = false;
    }
    else
    {
        PendingFocusPosition_ = position;
        HasPendingFocusPosition_ = true;
    }
}

void WorldPartitionRuntime::SetFocusCapsule(float radius, float height)
{
    FocusCapsuleRadius_ = std::max(0.0f, radius);
    FocusCapsuleCylinderHalfHeight_ = std::max(
        0.0f, height * 0.5f - FocusCapsuleRadius_);
}

void WorldPartitionRuntime::RelocateFocus(Vec3d position)
{
    if (!HasManifest_)
        return;
    Focus_ = ResolveFocusZone(Manifest_, position, {});
    FocusPosition_ = position;
    DockSweepPosition_ = position;
    HasFocusPosition_ = true;
    HasPendingFocusPosition_ = false;
    SuppressedDock_ = {};
    LastTraversal_ = {};
    TraversalGrace_ = {};
}

void WorldPartitionRuntime::SetFocus(ZoneId zone)
{
    assert(HasManifest_ && FindHeader(zone) != nullptr && "SetFocus: unknown zone");
    Focus_ = zone;
    SuppressedDock_ = {};
    LastTraversal_ = {};
    TraversalGrace_ = {};
    HasPendingFocusPosition_ = false;
    if (const ZoneHeader* header = FindHeader(zone); header != nullptr
        && header->Bounds.IsValid())
    {
        FocusPosition_ = header->Bounds.Center();
        DockSweepPosition_ = FocusPosition_;
        HasFocusPosition_ = true;
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

    LastTraversal_ = {};
    if (HasManifest_ && Focus_.IsValid() && HasPendingFocusPosition_)
    {
        std::vector<ZoneId> residentPhysicsZones;
        for (const ZoneHeader& zone : Manifest_.Zones)
        {
            const RuntimeZoneRecord* resident = world.FindZone(zone.Id);
            if (resident != nullptr && resident->Participation.Physics)
                residentPhysicsZones.push_back(zone.Id);
        }

        ZoneFocusState state{ Focus_, {}, SuppressedDock_, DockSweepPosition_ };
        LastTraversal_ = AdvanceZoneFocus(
            state, Index_, PendingFocusPosition_,
            DockCrossingOptions{
                .CapsuleRadius = FocusCapsuleRadius_,
                .CapsuleHalfHeight = FocusCapsuleCylinderHalfHeight_,
                .ResidentPhysicsZones = residentPhysicsZones,
                .RequireResidentDestination = true,
            });
        if (LastTraversal_.Status
            == DockTraversalStatus::BlockedDestinationNotReady)
        {
            ++LateTraversalCount_;
        }
        Focus_ = state.Current;
        SuppressedDock_ = state.SuppressedDock;
        DockSweepPosition_ = state.PreviousPosition;
        FocusPosition_ = LastTraversal_.Status
                == DockTraversalStatus::BlockedDestinationNotReady
            ? LastTraversal_.SafeSourcePosition : PendingFocusPosition_;
        HasFocusPosition_ = true;
        HasPendingFocusPosition_ = false;
        if (LastTraversal_.Status == DockTraversalStatus::Crossed)
            TraversalGrace_ = { LastTraversal_.From, 0.0 };
    }

    std::vector<ZoneDemandRecord> demand;
    std::vector<ZoneHopRank> ranks;
    if (HasManifest_ && Focus_.IsValid())
    {
        const WorldPartitionStreamingConfig resolved =
            ResolveGraphStreamingConfig(Manifest_, Focus_, Config_);
        ranks = ComputeZoneHopRanks(Manifest_, Index_, Focus_, resolved.HopCount);

        // Leases use the existing pin-shaped pure demand input, but remain
        // independently tokenized in the runtime. Duplicate entries are safe:
        // ComputeZoneDemand ORs every floor onto the same zone.
        std::vector<ZonePin> effectivePins = Pins_;
        effectivePins.reserve(Pins_.size() + ActiveLeaseCount_);
        for (const ParticipationLeaseSlot& lease : LeaseSlots_)
            if (lease.Alive)
                effectivePins.push_back(ZonePin{ lease.Zone, lease.Minimum });

        demand = ComputeZoneDemand(Manifest_, Index_, Focus_, effectivePins, resolved,
                                   HasFocusPosition_ ? &FocusPosition_ : nullptr);
    }

    if (TraversalGrace_.Zone.IsValid())
    {
        TraversalGrace_.Seconds += deltaSeconds;
        if (TraversalGrace_.Seconds <= std::max(0.0, Config_.LingerSeconds))
        {
            ZoneDemandRecord* previous = nullptr;
            for (ZoneDemandRecord& record : demand)
                if (record.Zone == TraversalGrace_.Zone)
                    previous = &record;
            if (previous == nullptr)
            {
                ZoneDemandRecord record;
                record.Zone = TraversalGrace_.Zone;
                record.Desired = ZoneParticipation{
                    .Visible = Config_.NeighborVisible,
                    .Physics = Config_.NeighborPhysics,
                };
                demand.push_back(std::move(record));
                previous = &demand.back();
            }
            previous->Sources.TraversalGrace = true;
            AddRuntimeReason(*previous,
                             { ZoneDemandReason::TraversalGrace, Focus_,
                               LastTraversal_.Status
                                       == DockTraversalStatus::Crossed
                                   ? LastTraversal_.Dock.Value : 0,
                               0, {} });
        }
        else
        {
            TraversalGrace_ = {};
        }
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
        record.Sources.Lingering = true;
        record.Sources.Linger = true;
        AddRuntimeReason(record, { ZoneDemandReason::Linger, Focus_, 0, 0, {} });
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
        record.Sources.Linger = true;
        AddRuntimeReason(record, { ZoneDemandReason::Linger, Focus_, 0, 0, {} });
        Records_.push_back(record);
    }
    std::sort(Records_.begin(), Records_.end(),
              [](const ZoneDemandRecord& a, const ZoneDemandRecord& b)
              { return a.Zone.Value < b.Zone.Value; });
}
