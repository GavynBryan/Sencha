#include <spatial/candidates/CandidateScratch.h>

CandidateScratch::CandidateScratch(const CandidateLimits& limits)
    : Limits_(limits)
    , Reachable(limits.MaxReachableRegions)
{
    const std::size_t rows = limits.MaxCandidates;
    const std::size_t cells = rows * limits.MaxCriteria;
    Positions.resize(rows);
    Entities.resize(rows);
    Navs.resize(rows);
    NavTried.resize(rows);
    Generators.resize(rows);
    RejectedBy.resize(rows);
    RejectStatus.resize(rows);
    Scores.resize(rows);
    Values.resize(cells);
    Statuses.resize(cells);
    CurveOutputs.resize(cells);
    Live.reserve(rows);
    Applicable.reserve(rows);
    Ranked.reserve(rows);
    EntitySelection.reserve(rows);
    ReachableByRef.reserve(limits.MaxReachableRegions);
}

CandidateResultBuffer::CandidateResultBuffer(std::uint32_t capacity)
    : Entries_(capacity)
{
}
