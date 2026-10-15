#include <spatial/candidates/CandidateTrace.h>

CandidateTrace::CandidateTrace(CandidateTraceLevel level,
                               std::uint32_t candidateCapacity,
                               std::uint32_t criterionCapacity)
    : Level_(level)
    , Criteria_(criterionCapacity)
{
    if (level == CandidateTraceLevel::Full)
    {
        Rows_.resize(candidateCapacity);
        Values_.resize(static_cast<std::size_t>(candidateCapacity) * criterionCapacity);
    }
}

void CandidateTrace::Reset()
{
    Result = {};
    DroppedAtGeneration = 0;
    ElapsedMicroseconds = 0.0;
    for (CandidateTraceCriterion& criterion : Criteria_)
    {
        criterion.Evaluated = 0;
        criterion.Rejected = 0;
        criterion.Statuses.fill(0);
        criterion.ExactNavSearches = 0;
        criterion.Raycasts = 0;
    }
    CriterionCount = 0;
    for (CandidateTraceValue& value : Values_)
        value = {};
    RowCount = 0;
    Truncated_ = false;
}
