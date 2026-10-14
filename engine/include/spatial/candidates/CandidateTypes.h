#pragma once

#include <ecs/EntityId.h>
#include <math/Vec.h>
#include <navigation/NavigationTypes.h>

#include <cstdint>
#include <optional>
#include <span>

class StoragePartitionSet;

// Measures run cheapest class first, so expensive ones see only survivors.
enum class CandidateCostClass : std::uint8_t
{
    Geometric,
    Entity,
    NavigationBatch,
    Physics,
    NavigationExact,
};

enum class CandidateAppliesTo : std::uint8_t
{
    Any,
    Location,
    Entity,
};

enum class CandidateValueKind : std::uint8_t
{
    // Arbitrary units; scoring needs an authored curve.
    Scalar,
    // Already in [0, 1] (booleans, shares); scores it directly unless a curve is given.
    UnitInterval,
};

enum class CandidateMeasureStatus : std::uint8_t
{
    Ok,
    // Measured by an approximation the criterion opted into.
    Estimated,
    NotApplicable,
    Unreachable,
    // Outside a cost limit, or the run's search or ray budget ran out first.
    BeyondBudget,
    // In a different zone from the querier.
    OutsideZone,
    // In the querier's zone but not on its navigation.
    OffNavigation,
    Stale,
    Failed,
    Count,
};

[[nodiscard]] const char* CandidateMeasureStatusName(CandidateMeasureStatus status);

[[nodiscard]] constexpr bool IsMeasured(CandidateMeasureStatus status)
{
    return status == CandidateMeasureStatus::Ok || status == CandidateMeasureStatus::Estimated;
}

enum class CandidateRunStatus : std::uint8_t
{
    Success,
    NoCandidates,
    NoneQualified,
    ContextMissing,
    DefinitionStale,
    ScratchTooSmall,
    // The querier's zone is named but not resident.
    QuerierZoneUnavailable,
};

[[nodiscard]] const char* CandidateRunStatusName(CandidateRunStatus status);

struct CandidateRunFlags
{
    bool GenerationTruncated = false;
    bool OutputTruncated = false;
    bool BudgetExhausted = false;
    bool QuerierOffNavigation = false;
    bool UsedEstimates = false;
    // The querier's zone has no navigation data, or no zone was named.
    bool NavigationUnavailable = false;

    friend bool operator==(const CandidateRunFlags&, const CandidateRunFlags&) = default;
};

// An entity-bearing point takes its position and forward from the entity's
// WorldTransform at run time; Position and Forward are then ignored.
struct CandidatePoint
{
    Vec3d Position = Vec3d::Zero();
    Vec3d Forward = Vec3d::Zero();
    EntityId Entity{};
};

struct CandidateSlotBinding
{
    std::uint8_t Slot = 0;
    std::span<const CandidatePoint> Points;
};

// Everything one run is asked about. Positions come from the same snapshot as
// the candidates: see docs/spatial/candidates.md, "Freshness".
struct CandidateContext
{
    // When it has a WorldTransform, Origin and Forward are read from it.
    EntityId Querier;
    Vec3d Origin = Vec3d::Zero();
    Vec3d Forward = Vec3d::Zero();

    // The last valid location, when the caller has one; otherwise the run
    // projects the querier once.
    std::optional<NavLocation> QuerierNav;

    // Passed to navigation unchanged. Navigation.Zone is the querier's zone.
    NavQueryRequest Navigation;

    std::span<const CandidateSlotBinding> Slots;

    // The caller phase's partitions. Null means the persistent partition plus
    // the querier's zone.
    const StoragePartitionSet* Partitions = nullptr;

    // PickFromBand only.
    std::uint64_t Seed = 0;
    // Trace only.
    std::uint64_t Tick = 0;
};

// Capacities a scratch is sized for.
struct CandidateLimits
{
    std::uint32_t MaxCandidates = 256;
    std::uint32_t MaxCriteria = 16;
    std::uint32_t MaxReachableRegions = 512;
};

struct CandidateResultEntry
{
    Vec3d Position = Vec3d::Zero();
    EntityId Entity{};
    NavLocation Nav;
    float Score = 0.0f;
    std::uint32_t Order = 0;
    std::uint32_t Rank = 0;
};

struct CandidateRunResult
{
    CandidateRunStatus Status = CandidateRunStatus::NoCandidates;
    CandidateRunFlags Flags;
    std::uint32_t Generated = 0;
    std::uint32_t Qualified = 0;
    std::uint32_t Returned = 0;
};
