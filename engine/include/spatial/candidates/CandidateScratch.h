#pragma once

#include <authored/AuthoredValue.h>
#include <spatial/candidates/CandidateTypes.h>
#include <ecs/StoragePartitionSet.h>
#include <navigation/NavQueryContext.h>
#include <navigation/NavigationTypes.h>

#include <cstdint>
#include <span>
#include <vector>

// Reusable storage for runs, sized once from its limits. After the first run
// sizes navigation's context, a run allocates nothing. One per caller thread;
// runs are owner-thread only.
class CandidateScratch
{
public:
    explicit CandidateScratch(const CandidateLimits& limits = {});

    [[nodiscard]] const CandidateLimits& Limits() const { return Limits_; }

private:
    friend class CandidateEvaluator;
    friend class CandidateRun;

    CandidateLimits Limits_;

    // One row per candidate.
    std::vector<Vec3d> Positions;
    std::vector<EntityId> Entities;
    std::vector<NavLocation> Navs;
    std::vector<std::uint8_t> NavTried;
    std::vector<std::uint8_t> Generators;
    std::vector<std::uint8_t> RejectedBy;
    std::vector<CandidateMeasureStatus> RejectStatus;
    std::vector<float> Scores;
    std::uint32_t Count = 0;

    // Criterion-major: criterion * MaxCandidates + row.
    std::vector<float> Values;
    std::vector<CandidateMeasureStatus> Statuses;
    std::vector<float> CurveOutputs;

    std::vector<std::uint32_t> Live;
    std::vector<std::uint32_t> Applicable;
    std::vector<std::uint32_t> Ranked;
    std::vector<EntityId> EntitySelection;

    NavQueryContext Navigation;
    NavReachableBuffer Reachable;
    std::vector<NavReachableRegion> ReachableByRef;
    NavRouteBuffer Route;
    StoragePartitionSet Partitions;
    AuthoredArguments QueryArguments;
    AuthoredValue QueryAnswer;
};

// Where a run writes what it returns. Sized once; never grows.
class CandidateResultBuffer
{
public:
    explicit CandidateResultBuffer(std::uint32_t capacity);

    [[nodiscard]] std::span<const CandidateResultEntry> Entries() const
    {
        return { Entries_.data(), Count };
    }
    [[nodiscard]] std::uint32_t Capacity() const { return static_cast<std::uint32_t>(Entries_.size()); }

private:
    friend class CandidateEvaluator;

    std::vector<CandidateResultEntry> Entries_;
    std::uint32_t Count = 0;
};
