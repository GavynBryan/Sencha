#pragma once

#include <spatial/candidates/CandidateTrace.h>
#include <spatial/candidates/CandidateTypes.h>
#include <ecs/Query.h>
#include <ecs/QueryAccessors.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <physics/PhysicsQueries.h>
#include <world/transform/TransformComponents.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class AuthoredArguments;
class AuthoredQueryDispatcher;
class AuthoredValue;
class CandidateEvaluation;
class CandidateEvaluator;
class CandidateScratch;
class GameplayTagRegistry;
class NavQueryContext;
class NavRouteBuffer;
class RuntimeWorld;
class World;
class ZoneNavigation;

using CandidateTaggedEntityQuery = Query<Read<WorldTransform>, Read<GameplayTagContainer>>;

// One run as its operations see it: the querier, the bound slots, the
// candidates so far, and the services and budgets they may spend. Built by
// CandidateEvaluator for the duration of one Evaluate call.
class CandidateRun
{
public:
    CandidateRun(const CandidateRun&) = delete;
    CandidateRun& operator=(const CandidateRun&) = delete;

    // -- Querier and slots ----------------------------------------------------
    [[nodiscard]] EntityId Querier() const { return QuerierEntity; }
    [[nodiscard]] const Vec3d& QuerierPosition() const { return QuerierPoint.Position; }
    [[nodiscard]] const Vec3d& QuerierForward() const { return QuerierPoint.Forward; }
    [[nodiscard]] ZoneId QuerierZone() const;

    // Slot 0 is the querier. Entity-bearing points are read from WorldTransform.
    [[nodiscard]] std::uint32_t SlotSize(std::uint8_t slot) const;
    [[nodiscard]] CandidatePoint SlotPoint(std::uint8_t slot, std::uint32_t index) const;

    // -- Candidates -----------------------------------------------------------
    [[nodiscard]] std::uint32_t CandidateCount() const;
    [[nodiscard]] const Vec3d& Position(std::uint32_t row) const;
    [[nodiscard]] EntityId Entity(std::uint32_t row) const;

    // False once the set is full. An entity already present is not added twice.
    bool Append(const Vec3d& position, EntityId entity = {}, const NavLocation& nav = {});
    [[nodiscard]] bool Full() const;
    [[nodiscard]] std::uint32_t Room() const;
    void MarkGenerationTruncated() { Flags.GenerationTruncated = true; }
    void CountDroppedAtGeneration();
    // Reusable storage for a generator's own selection pass.
    [[nodiscard]] std::vector<EntityId>& EntitySelection();

    // -- Services -------------------------------------------------------------
    [[nodiscard]] const World& Entities() const;
    [[nodiscard]] const RuntimeWorld& Runtime() const { return RuntimeState; }
    [[nodiscard]] const GameplayTagRegistry* Tags() const;
    [[nodiscard]] const PhysicsQueries* Physics() const { return PhysicsState ? &*PhysicsState : nullptr; }
    [[nodiscard]] const AuthoredQueryDispatcher* Queries() const { return QueryDispatcher; }
    // Built on first use, then kept by the evaluator.
    [[nodiscard]] CandidateTaggedEntityQuery& TaggedEntities();
    [[nodiscard]] const StoragePartitionSet& Partitions() const { return *PartitionSet; }
    [[nodiscard]] std::optional<ZoneId> ZoneOf(EntityId entity) const;

    // -- Navigation -----------------------------------------------------------
    // Null when the querier's zone has no navigation.
    [[nodiscard]] const ZoneNavigation* Navigation() const { return ZoneNav; }
    [[nodiscard]] const NavQueryRequest& NavRequest() const;
    [[nodiscard]] NavQueryContext& NavContext();
    [[nodiscard]] NavRouteBuffer& Route();

    // Projected once per run; nullopt sets QuerierOffNavigation.
    [[nodiscard]] std::optional<NavLocation> QuerierNav();

    // The candidate's location in the querier's zone, projecting it once if the
    // generator did not. Ok, OutsideZone, OffNavigation or NotApplicable.
    [[nodiscard]] CandidateMeasureStatus CandidateNav(std::uint32_t row, NavLocation& out);

    // Built once per run with the first caller's limits, then shared.
    // Nullopt when navigation or the querier's location is unavailable.
    [[nodiscard]] std::optional<std::span<const NavReachableRegion>> ReachableRegions(float radius, float maxCost);
    [[nodiscard]] const NavReachableRegion* FindReachable(std::uint64_t regionRef) const;

    // -- Budgets --------------------------------------------------------------
    // False, and BudgetExhausted set, once the run's allowance is spent.
    [[nodiscard]] bool ChargeExactNavSearch();
    [[nodiscard]] bool ChargeRaycast();

    void MarkEstimated() { Flags.UsedEstimates = true; }
    void MarkDefinitionStale() { DefinitionStale = true; }

    // -- Authored-query scratch -----------------------------------------------
    [[nodiscard]] AuthoredArguments& QueryArguments();
    [[nodiscard]] AuthoredValue& QueryAnswer();

    // -- Trace ----------------------------------------------------------------
    [[nodiscard]] bool TracingDetail() const;
    void RecordDetail(std::uint32_t row, const CandidateDetail& detail);

private:
    friend class CandidateEvaluator;

    CandidateRun(CandidateEvaluator& evaluator,
                 const CandidateEvaluation& evaluation,
                 const CandidateContext& context,
                 CandidateScratch& scratch,
                 CandidateTrace* trace);

    CandidateEvaluator& Evaluator;
    const RuntimeWorld& RuntimeState;
    const AuthoredQueryDispatcher* QueryDispatcher;
    std::optional<PhysicsQueries> PhysicsState;
    const CandidateEvaluation& Evaluation;
    const CandidateContext& Context;
    CandidateScratch& Scratch;
    CandidateTrace* Trace;

    EntityId QuerierEntity;
    CandidatePoint QuerierPoint;
    const ZoneNavigation* ZoneNav = nullptr;
    const StoragePartitionSet* PartitionSet = nullptr;

    std::optional<NavLocation> QuerierLocation;
    bool QuerierProjected = false;
    bool ReachableBuilt = false;
    bool ReachableAvailable = false;
    std::uint32_t ExactNavSearches = 0;
    std::uint32_t Raycasts = 0;
    std::uint32_t DroppedAtGeneration = 0;
    std::uint32_t CurrentCriterion = 0;
    std::uint8_t CurrentGenerator = 0;
    bool DefinitionStale = false;
    CandidateRunFlags Flags;
};
