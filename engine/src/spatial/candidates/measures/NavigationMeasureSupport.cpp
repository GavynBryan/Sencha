#include "NavigationMeasureSupport.h"

namespace candidate_operation
{
    namespace
    {
        [[nodiscard]] CandidateMeasureStatus FromSearch(NavStatus status)
        {
            switch (status)
            {
            case NavStatus::Success: return CandidateMeasureStatus::Ok;
            case NavStatus::NoPath: return CandidateMeasureStatus::Unreachable;
            case NavStatus::StaleLocation: return CandidateMeasureStatus::Stale;
            default: return CandidateMeasureStatus::Failed;
            }
        }
    }

    NavigationMeasureState ReadNavigationMeasure(const JsonValue& arguments,
                                                 std::string_view fidelityKey,
                                                 std::string_view exactChoice)
    {
        NavigationMeasureState state;
        state.Fidelity = StringOr(arguments, fidelityKey, "") == exactChoice ? NavigationFidelity::Exact
                                                                             : NavigationFidelity::RegionEntry;
        state.MaxCost = FloatOr(arguments, "max_cost", std::numeric_limits<float>::infinity());
        state.Outside = ReadOutsideZone(arguments);
        return state;
    }

    NavigationAnswer AskNavigation(CandidateRun& run, const NavigationMeasureState& state, std::uint32_t row)
    {
        NavLocation target;
        const CandidateMeasureStatus located = run.CandidateNav(row, target);
        if (located == CandidateMeasureStatus::OutsideZone && state.Outside == OutsideZone::Estimate)
            return { CandidateMeasureStatus::Estimated, (run.Position(row) - run.QuerierPosition()).Magnitude() };
        if (located != CandidateMeasureStatus::Ok)
            return { located };

        const std::optional<NavLocation> start = run.QuerierNav();
        if (!start)
            return { CandidateMeasureStatus::Failed };

        if (state.Fidelity == NavigationFidelity::RegionEntry)
        {
            if (!run.ReachableRegions(std::numeric_limits<float>::infinity(), state.MaxCost))
                return { CandidateMeasureStatus::Failed };
            const NavReachableRegion* region = run.FindReachable(target.Ref);
            if (region == nullptr)
                return { CandidateMeasureStatus::BeyondBudget };
            return { CandidateMeasureStatus::Ok, region->EntryCost };
        }

        if (!run.ChargeExactNavSearch())
            return { CandidateMeasureStatus::BeyondBudget };
        const NavCostResult cost = NavTravelCost(*run.Navigation(), run.NavContext(), run.NavRequest(), *start, target);
        NavigationAnswer answer{ FromSearch(cost.Status), cost.Cost, cost.Status };
        if (answer.Status == CandidateMeasureStatus::Ok && cost.Cost > state.MaxCost)
            answer.Status = CandidateMeasureStatus::BeyondBudget;
        return answer;
    }
}
