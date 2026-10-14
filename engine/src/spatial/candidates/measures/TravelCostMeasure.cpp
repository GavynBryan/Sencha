#include "NavigationMeasureSupport.h"

namespace candidate_operation
{
    namespace
    {
        CandidatePrepared PrepareTravelCost(const JsonValue& arguments, const CandidatePrepareContext&,
                                            std::string_view, std::vector<std::string>&)
        {
            const NavigationMeasureState state = ReadNavigationMeasure(arguments, "fidelity", "exact");
            return Prepared(state, state.Fidelity == NavigationFidelity::Exact
                                       ? std::optional(CandidateCostClass::NavigationExact)
                                       : std::nullopt);
        }

        void MeasureTravelCost(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                               std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& travel = *static_cast<const NavigationMeasureState*>(state);
            for (const std::uint32_t row : rows)
            {
                const NavigationAnswer answer = AskNavigation(run, travel, row);
                statuses[row] = answer.Status;
                values[row] = answer.Cost;
                if (run.TracingDetail())
                    run.RecordDetail(row, CandidateDetail{ .Code = static_cast<std::uint32_t>(answer.Search) });
            }
        }
    }

    CandidateMeasureDefinition TravelCostMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.travel_cost";
        definition.DisplayName = "Travel cost";
        definition.Description = "Navigation cost from the querier, in the policy's cost units. region_entry is "
                                 "the cost of entering the candidate's region (low by up to one region's width); "
                                 "exact searches each candidate.";
        definition.Arguments = Record({ EnumField("fidelity", "Fidelity", { "region_entry", "exact" }, "region_entry"),
                                        FloatField("max_cost", "Maximum cost", std::nullopt, 0.0),
                                        OutsideZoneField() });
        definition.Arguments.Children[1].Required = false;
        definition.Cost = CandidateCostClass::NavigationBatch;
        definition.Prepare = PrepareTravelCost;
        definition.Measure = MeasureTravelCost;
        return definition;
    }
}
