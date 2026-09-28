#pragma once

#include "../CandidateOperationSupport.h"

#include <navigation/NavigationQuery.h>

#include <limits>

namespace candidate_operation
{
    enum class NavigationFidelity : std::uint8_t
    {
        // One shared reachable-region search per run.
        RegionEntry,
        // One route search per candidate, charged to the run's budget.
        Exact,
    };

    struct NavigationMeasureState
    {
        NavigationFidelity Fidelity = NavigationFidelity::RegionEntry;
        float MaxCost = std::numeric_limits<float>::infinity();
        OutsideZone Outside = OutsideZone::Reject;
    };

    // One candidate's answer from navigation: its cost from the querier and
    // the status that goes with it. Shared by `reachable` and `travel_cost`,
    // which differ only in what they report.
    struct NavigationAnswer
    {
        CandidateMeasureStatus Status = CandidateMeasureStatus::Failed;
        float Cost = 0.0f;
        NavStatus Search = NavStatus::Success;
    };

    [[nodiscard]] NavigationAnswer AskNavigation(CandidateRun& run, const NavigationMeasureState& state, std::uint32_t row);

    [[nodiscard]] NavigationMeasureState ReadNavigationMeasure(const JsonValue& arguments,
                                                               std::string_view fidelityKey,
                                                               std::string_view exactChoice);
}
