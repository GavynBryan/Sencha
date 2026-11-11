#include <spatial/candidates/CandidateTrace.h>

#include <format>

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

std::string DescribeCandidateTrace(const CandidateTrace& trace)
{
    const CandidateRunResult& result = trace.Result;
    std::string text = std::format("  last place run '{}': {}, generated {}, qualified {}{}\n", trace.Evaluation,
                                   CandidateRunStatusName(result.Status), result.Generated, result.Qualified,
                                   result.Flags.BudgetExhausted ? ", budget spent" : "");
    for (const CandidateTraceCriterion& criterion : trace.Criteria())
    {
        if (criterion.Mode != ConsiderationMode::Require)
            continue;
        text += std::format("    {} rejected {} of {}", criterion.Measure, criterion.Rejected, criterion.Evaluated);
        for (std::size_t status = 0; status < criterion.Statuses.size(); ++status)
            if (status != static_cast<std::size_t>(CandidateMeasureStatus::Ok) && criterion.Statuses[status] > 0)
                text += std::format(", {} {}", criterion.Statuses[status],
                                    CandidateMeasureStatusName(static_cast<CandidateMeasureStatus>(status)));
        text += "\n";
    }
    return text;
}
