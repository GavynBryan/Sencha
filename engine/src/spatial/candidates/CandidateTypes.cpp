#include <spatial/candidates/CandidateTypes.h>

const char* CandidateMeasureStatusName(CandidateMeasureStatus status)
{
    switch (status)
    {
    case CandidateMeasureStatus::Ok: return "Ok";
    case CandidateMeasureStatus::Estimated: return "Estimated";
    case CandidateMeasureStatus::NotApplicable: return "NotApplicable";
    case CandidateMeasureStatus::Unreachable: return "Unreachable";
    case CandidateMeasureStatus::BeyondBudget: return "BeyondBudget";
    case CandidateMeasureStatus::OutsideZone: return "OutsideZone";
    case CandidateMeasureStatus::OffNavigation: return "OffNavigation";
    case CandidateMeasureStatus::Stale: return "Stale";
    case CandidateMeasureStatus::Failed: return "Failed";
    case CandidateMeasureStatus::Count: break;
    }
    return "Unknown";
}

const char* CandidateRunStatusName(CandidateRunStatus status)
{
    switch (status)
    {
    case CandidateRunStatus::Success: return "Success";
    case CandidateRunStatus::NoCandidates: return "NoCandidates";
    case CandidateRunStatus::NoneQualified: return "NoneQualified";
    case CandidateRunStatus::ContextMissing: return "ContextMissing";
    case CandidateRunStatus::DefinitionStale: return "DefinitionStale";
    case CandidateRunStatus::ScratchTooSmall: return "ScratchTooSmall";
    case CandidateRunStatus::QuerierZoneUnavailable: return "QuerierZoneUnavailable";
    }
    return "Unknown";
}
