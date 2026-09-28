#include <spatial/candidates/CandidateRun.h>

#include <spatial/candidates/CandidateEvaluation.h>
#include <spatial/candidates/CandidateEvaluator.h>
#include <spatial/candidates/CandidateScratch.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <navigation/NavigationQuery.h>
#include <navigation/ZoneNavigation.h>
#include <world/RuntimeWorld.h>

#include <algorithm>
#include <cassert>

namespace
{
    // Extents for projecting a candidate a generator left unprojected.
    const Vec3d kCandidateProjectionExtents(1.0f, 2.0f, 1.0f);

    constexpr std::uint8_t kAlive = 0xFF;

    [[nodiscard]] CandidatePoint FromEntity(const World& world, const CandidatePoint& point)
    {
        if (!point.Entity.IsValid() || !world.IsAlive(point.Entity))
            return point;
        const WorldTransform* transform = world.TryGet<WorldTransform>(point.Entity);
        if (transform == nullptr)
            return point;
        return CandidatePoint{ transform->Value.Position, transform->Value.Forward(), point.Entity };
    }
}

CandidateRun::CandidateRun(CandidateEvaluator& evaluator,
                           const CandidateEvaluation& evaluation,
                           const CandidateContext& context,
                           CandidateScratch& scratch,
                           CandidateTrace* trace)
    : Evaluator(evaluator)
    , RuntimeState(evaluator.Runtime)
    , QueryDispatcher(evaluator.Queries)
    , Evaluation(evaluation)
    , Context(context)
    , Scratch(scratch)
    , Trace(trace)
    , QuerierEntity(context.Querier)
{
    if (evaluator.Physics != nullptr)
        PhysicsState.emplace(*evaluator.Physics);

    QuerierPoint = FromEntity(Entities(), CandidatePoint{ context.Origin, context.Forward, context.Querier });

    const ZoneId zone = context.Navigation.Zone;
    if (zone.IsValid())
        ZoneNav = FindZoneNavigation(RuntimeState, zone);

    if (context.Partitions != nullptr)
        PartitionSet = context.Partitions;
    else
    {
        Scratch.Partitions.Clear();
        (void)Scratch.Partitions.Add(PersistentStoragePartition);
        if (const RuntimeZoneRecord* record = zone.IsValid() ? RuntimeState.FindZone(zone) : nullptr)
            (void)Scratch.Partitions.Add(record->Partition);
        PartitionSet = &Scratch.Partitions;
    }

    Scratch.Count = 0;
    Scratch.ReachableByRef.clear();
}

ZoneId CandidateRun::QuerierZone() const
{
    return Context.Navigation.Zone;
}

std::uint32_t CandidateRun::SlotSize(std::uint8_t slot) const
{
    if (slot == 0)
        return 1;
    for (const CandidateSlotBinding& binding : Context.Slots)
    {
        if (binding.Slot == slot)
            return static_cast<std::uint32_t>(binding.Points.size());
    }
    return 0;
}

CandidatePoint CandidateRun::SlotPoint(std::uint8_t slot, std::uint32_t index) const
{
    if (slot == 0)
        return QuerierPoint;
    for (const CandidateSlotBinding& binding : Context.Slots)
    {
        if (binding.Slot == slot)
            return FromEntity(Entities(), binding.Points[index]);
    }
    assert(false && "SlotPoint on an unbound slot");
    return {};
}

std::uint32_t CandidateRun::CandidateCount() const
{
    return Scratch.Count;
}

const Vec3d& CandidateRun::Position(std::uint32_t row) const
{
    return Scratch.Positions[row];
}

EntityId CandidateRun::Entity(std::uint32_t row) const
{
    return Scratch.Entities[row];
}

bool CandidateRun::Full() const
{
    return Scratch.Count >= Evaluation.Budgets().Candidates;
}

bool CandidateRun::Append(const Vec3d& position, EntityId entity, const NavLocation& nav)
{
    if (Full())
    {
        Flags.GenerationTruncated = true;
        return false;
    }
    if (entity.IsValid())
    {
        const auto end = Scratch.Entities.begin() + Scratch.Count;
        if (std::find(Scratch.Entities.begin(), end, entity) != end)
            return true;
    }
    const std::uint32_t row = Scratch.Count++;
    Scratch.Positions[row] = position;
    Scratch.Entities[row] = entity;
    Scratch.Navs[row] = nav;
    Scratch.NavTried[row] = nav.IsValid() ? 1 : 0;
    Scratch.Generators[row] = CurrentGenerator;
    Scratch.RejectedBy[row] = kAlive;
    Scratch.RejectStatus[row] = CandidateMeasureStatus::Ok;
    Scratch.Scores[row] = 0.0f;
    return true;
}

std::uint32_t CandidateRun::Room() const
{
    const std::uint32_t budget = Evaluation.Budgets().Candidates;
    return Scratch.Count < budget ? budget - Scratch.Count : 0;
}

std::vector<EntityId>& CandidateRun::EntitySelection()
{
    return Scratch.EntitySelection;
}

void CandidateRun::CountDroppedAtGeneration()
{
    ++DroppedAtGeneration;
}

const World& CandidateRun::Entities() const
{
    return RuntimeState.Entities();
}

const GameplayTagRegistry* CandidateRun::Tags() const
{
    return Entities().TryGetResource<GameplayTagRegistry>();
}

CandidateTaggedEntityQuery& CandidateRun::TaggedEntities()
{
    if (!Evaluator.Tagged)
        Evaluator.Tagged.emplace(Entities());
    return *Evaluator.Tagged;
}

std::optional<ZoneId> CandidateRun::ZoneOf(EntityId entity) const
{
    if (!Entities().IsAlive(entity))
        return std::nullopt;
    const StoragePartitionId partition = Entities().GetEntityPartition(entity);
    if (partition == PersistentStoragePartition)
        return std::nullopt;
    const RuntimeZoneRecord* record = RuntimeState.FindPartition(partition);
    return record != nullptr ? std::optional<ZoneId>(record->Id) : std::nullopt;
}

const NavQueryRequest& CandidateRun::NavRequest() const
{
    return Context.Navigation;
}

NavQueryContext& CandidateRun::NavContext()
{
    return Scratch.Navigation;
}

NavRouteBuffer& CandidateRun::Route()
{
    return Scratch.Route;
}

std::optional<NavLocation> CandidateRun::QuerierNav()
{
    if (QuerierProjected)
        return QuerierLocation;
    QuerierProjected = true;
    if (Context.QuerierNav && Context.QuerierNav->IsValid())
        QuerierLocation = Context.QuerierNav;
    else if (ZoneNav != nullptr)
    {
        const NavProjectResult projected = NavProjectPoint(*ZoneNav, Scratch.Navigation, Context.Navigation,
                                                           QuerierPoint.Position, kCandidateProjectionExtents);
        if (projected.Status == NavStatus::Success)
            QuerierLocation = projected.Location;
    }
    if (!QuerierLocation)
        Flags.QuerierOffNavigation = true;
    return QuerierLocation;
}

CandidateMeasureStatus CandidateRun::CandidateNav(std::uint32_t row, NavLocation& out)
{
    if (ZoneNav == nullptr)
        return CandidateMeasureStatus::NotApplicable;
    const EntityId entity = Scratch.Entities[row];
    if (entity.IsValid())
    {
        const std::optional<ZoneId> zone = ZoneOf(entity);
        if (zone && *zone != QuerierZone())
            return CandidateMeasureStatus::OutsideZone;
    }
    if (Scratch.Navs[row].IsValid())
    {
        out = Scratch.Navs[row];
        return CandidateMeasureStatus::Ok;
    }
    if (Scratch.NavTried[row] != 0)
        return CandidateMeasureStatus::OffNavigation;

    Scratch.NavTried[row] = 1;
    const NavProjectResult projected = NavProjectPoint(*ZoneNav, Scratch.Navigation, Context.Navigation,
                                                       Scratch.Positions[row], kCandidateProjectionExtents);
    if (projected.Status != NavStatus::Success)
        return CandidateMeasureStatus::OffNavigation;
    Scratch.Navs[row] = projected.Location;
    out = projected.Location;
    return CandidateMeasureStatus::Ok;
}

std::optional<std::span<const NavReachableRegion>> CandidateRun::ReachableRegions(float radius, float maxCost)
{
    if (!ReachableBuilt)
    {
        ReachableBuilt = true;
        const std::optional<NavLocation> start = QuerierNav();
        if (ZoneNav != nullptr && start)
        {
            const NavStatus status = NavCollectReachable(*ZoneNav, Scratch.Navigation, Context.Navigation, *start,
                                                         radius, maxCost, Scratch.Reachable);
            std::span<const NavReachableRegion> regions = Scratch.Reachable.Regions();
            const std::size_t budget = Evaluation.Budgets().ReachableRegions;
            if (status != NavStatus::Success || regions.size() > budget)
                Flags.BudgetExhausted = true;
            regions = regions.first(std::min(regions.size(), budget));
            Scratch.ReachableByRef.assign(regions.begin(), regions.end());
            std::ranges::sort(Scratch.ReachableByRef, {}, [](const NavReachableRegion& r) { return r.Region.Ref; });
            ReachableAvailable = true;
        }
    }
    if (!ReachableAvailable)
        return std::nullopt;
    return Scratch.Reachable.Regions().first(Scratch.ReachableByRef.size());
}

const NavReachableRegion* CandidateRun::FindReachable(std::uint64_t regionRef) const
{
    const auto found = std::ranges::lower_bound(Scratch.ReachableByRef, regionRef, {},
                                                [](const NavReachableRegion& r) { return r.Region.Ref; });
    return found != Scratch.ReachableByRef.end() && found->Region.Ref == regionRef ? &*found : nullptr;
}

bool CandidateRun::ChargeExactNavSearch()
{
    if (ExactNavSearches >= Evaluation.Budgets().ExactNavSearches)
    {
        Flags.BudgetExhausted = true;
        return false;
    }
    ++ExactNavSearches;
    if (Trace != nullptr && CurrentCriterion < Trace->Criteria_.size())
        ++Trace->Criteria_[CurrentCriterion].ExactNavSearches;
    return true;
}

bool CandidateRun::ChargeRaycast()
{
    if (Raycasts >= Evaluation.Budgets().Raycasts)
    {
        Flags.BudgetExhausted = true;
        return false;
    }
    ++Raycasts;
    if (Trace != nullptr && CurrentCriterion < Trace->Criteria_.size())
        ++Trace->Criteria_[CurrentCriterion].Raycasts;
    return true;
}

AuthoredArguments& CandidateRun::QueryArguments()
{
    return Scratch.QueryArguments;
}

AuthoredValue& CandidateRun::QueryAnswer()
{
    return Scratch.QueryAnswer;
}

bool CandidateRun::TracingDetail() const
{
    return Trace != nullptr && Trace->Level_ == CandidateTraceLevel::Full
        && CurrentCriterion < Trace->Criteria_.size();
}

void CandidateRun::RecordDetail(std::uint32_t row, const CandidateDetail& detail)
{
    if (!TracingDetail() || row >= Trace->Rows_.size())
        return;
    CandidateTraceValue& value =
        Trace->Values_[static_cast<std::size_t>(CurrentCriterion) * Trace->Rows_.size() + row];
    value.HasDetail = true;
    value.Detail = detail;
}
