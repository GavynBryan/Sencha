#include "NavigationMeasureSupport.h"

#include <algorithm>
#include <numbers>

namespace candidate_operation
{
    namespace
    {
        struct EscapeRoutesState
        {
            float Distance = 8.0f;
            float Detour = 1.5f;
            float Vertical = 3.0f;
            std::uint32_t Directions = 8;
        };

        CandidatePrepared PrepareEscapeRoutes(const JsonValue& arguments, const CandidatePrepareContext&,
                                              std::string_view, std::vector<std::string>&)
        {
            return Prepared(EscapeRoutesState{
                .Distance = FloatOr(arguments, "distance", 8.0f),
                .Detour = FloatOr(arguments, "detour", 1.5f),
                .Vertical = FloatOr(arguments, "vertical", 3.0f),
                .Directions = static_cast<std::uint32_t>(arguments.NumberOr("directions", 8)),
            });
        }

        [[nodiscard]] Vec3d Direction(std::uint32_t index, std::uint32_t directions)
        {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / static_cast<float>(directions);
            return Vec3d(std::cos(angle), 0.0f, std::sin(angle));
        }

        void MeasureEscapeRoutes(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                                 std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& escape = *static_cast<const EscapeRoutesState*>(state);
            NavReachableBuffer& flood = run.CandidateFlood();
            const Vec3d extents(0.5f, escape.Vertical, 0.5f);
            for (const std::uint32_t row : rows)
            {
                NavLocation location;
                statuses[row] = run.CandidateNav(row, location);
                if (statuses[row] != CandidateMeasureStatus::Ok)
                    continue;
                if (!run.ChargeExactNavSearch())
                {
                    statuses[row] = CandidateMeasureStatus::BeyondBudget;
                    continue;
                }
                const NavStatus search =
                    NavCollectReachable(*run.Navigation(), run.NavContext(), run.NavRequest(), location,
                                        std::numeric_limits<float>::infinity(), escape.Distance * escape.Detour, flood);
                if (search != NavStatus::Success && search != NavStatus::OutputCapacityReached)
                {
                    statuses[row] = CandidateMeasureStatus::Failed;
                    continue;
                }
                std::uint32_t open = 0;
                for (std::uint32_t index = 0; index < escape.Directions; ++index)
                {
                    const Vec3d probe = location.Position + Direction(index, escape.Directions) * escape.Distance;
                    const NavProjectResult projected =
                        NavProjectPoint(*run.Navigation(), run.NavContext(), run.NavRequest(), probe, extents);
                    if (projected.Status != NavStatus::Success)
                        continue;
                    const bool reached = std::ranges::any_of(flood.Regions(), [&](const NavReachableRegion& region) {
                        return region.Region.Ref == projected.Location.Ref;
                    });
                    open += reached ? 1u : 0u;
                }
                values[row] = static_cast<float>(open);
                if (run.TracingDetail())
                    run.RecordDetail(row, CandidateDetail{ .Code = static_cast<std::uint32_t>(search) });
            }
        }
    }

    CandidateMeasureDefinition EscapeRoutesMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.escape_routes";
        definition.DisplayName = "Escape routes";
        definition.Description = "Of evenly spaced directions around the candidate, how many lead to walkable "
                                 "ground the distance away that the candidate can reach within distance times "
                                 "detour. A dead end opens one way, a corridor two, open ground all of them. One "
                                 "search per candidate, charged to the run's exact-search budget.";
        DataFieldSchema directions = IntField("directions", "Directions", 8, 2);
        directions.Numeric.Maximum = 64.0;
        definition.Arguments = Record({ FloatField("distance", "Distance", 8.0, 0.0),
                                        FloatField("detour", "Detour allowance", 1.5, 1.0),
                                        FloatField("vertical", "Vertical search", 3.0, 0.0),
                                        std::move(directions) });
        definition.Cost = CandidateCostClass::NavigationExact;
        definition.AppliesTo = CandidateAppliesTo::Any;
        definition.Value = CandidateValueKind::Scalar;
        definition.Prepare = PrepareEscapeRoutes;
        definition.Measure = MeasureEscapeRoutes;
        return definition;
    }
}
