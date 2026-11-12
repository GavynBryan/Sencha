#include "NavigationMeasureSupport.h"

namespace candidate_operation
{
    namespace
    {
        struct EdgeDistanceState
        {
            float MaxRadius = 5.0f;
        };

        CandidatePrepared PrepareEdgeDistance(const JsonValue& arguments, const CandidatePrepareContext&,
                                              std::string_view, std::vector<std::string>&)
        {
            return Prepared(EdgeDistanceState{ FloatOr(arguments, "max_radius", 5.0f) });
        }

        void MeasureEdgeDistance(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                                 std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& edge = *static_cast<const EdgeDistanceState*>(state);
            for (const std::uint32_t row : rows)
            {
                NavLocation location;
                statuses[row] = run.CandidateNav(row, location);
                if (statuses[row] != CandidateMeasureStatus::Ok)
                    continue;
                const NavEdgeDistanceResult result =
                    NavDistanceToEdge(*run.Navigation(), run.NavContext(), run.NavRequest(), location, edge.MaxRadius);
                if (result.Status != NavStatus::Success)
                {
                    statuses[row] = CandidateMeasureStatus::Failed;
                    continue;
                }
                values[row] = result.Distance;
                if (result.Hit && run.TracingDetail())
                    run.RecordDetail(row, CandidateDetail{ .Point = result.Point });
            }
        }
    }

    CandidateMeasureDefinition EdgeDistanceMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.edge_distance";
        definition.DisplayName = "Edge distance";
        definition.Description = "Metres of walkable surface around the candidate before an edge, up to the "
                                 "maximum radius: half a corridor's width at its middle, zero against a wall.";
        definition.Arguments = Record({ FloatField("max_radius", "Maximum radius", 5.0, 0.0) });
        definition.Cost = CandidateCostClass::NavigationBatch;
        definition.AppliesTo = CandidateAppliesTo::Any;
        definition.Value = CandidateValueKind::Scalar;
        definition.Prepare = PrepareEdgeDistance;
        definition.Measure = MeasureEdgeDistance;
        return definition;
    }
}
