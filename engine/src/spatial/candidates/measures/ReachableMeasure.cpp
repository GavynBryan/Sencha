#include "NavigationMeasureSupport.h"

namespace candidate_operation
{
    namespace
    {
        CandidatePrepared PrepareReachable(const JsonValue& arguments, const CandidatePrepareContext&,
                                           std::string_view, std::vector<std::string>&)
        {
            const NavigationMeasureState state = ReadNavigationMeasure(arguments, "source", "exact");
            return Prepared(state, state.Fidelity == NavigationFidelity::Exact
                                       ? std::optional(CandidateCostClass::NavigationExact)
                                       : std::nullopt);
        }

        void MeasureReachable(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                              std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& reachable = *static_cast<const NavigationMeasureState*>(state);
            for (const std::uint32_t row : rows)
            {
                const NavigationAnswer answer = AskNavigation(run, reachable, row);
                statuses[row] = answer.Status;
                values[row] = IsMeasured(answer.Status) ? 1.0f : 0.0f;
                if (run.TracingDetail())
                    run.RecordDetail(row, CandidateDetail{ .Code = static_cast<std::uint32_t>(answer.Search) });
            }
        }
    }

    CandidateMeasureDefinition ReachableMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.reachable";
        definition.DisplayName = "Reachable";
        definition.Description = "1 when the querier can walk to the candidate within the cost limit. The "
                                 "reachable set is shared by the run; exact searches are charged to its budget.";
        definition.Arguments = Record({ EnumField("source", "Source", { "reachable_set", "exact" }, "reachable_set"),
                                        FloatField("max_cost", "Maximum cost", std::nullopt, 0.0),
                                        OutsideZoneField() });
        definition.Arguments.Children[1].Required = false;
        definition.Cost = CandidateCostClass::NavigationBatch;
        definition.Value = CandidateValueKind::UnitInterval;
        definition.Prepare = PrepareReachable;
        definition.Measure = MeasureReachable;
        return definition;
    }
}
