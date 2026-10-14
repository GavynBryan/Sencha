#include "SightMeasureSupport.h"

#include <navigation/NavigationQuery.h>

#include <algorithm>
#include <cmath>

namespace candidate_operation
{
    namespace
    {
        enum class Combine : std::uint8_t
        {
            // 1 when any observer sees any sample.
            Any,
            // The largest share of samples one observer sees.
            Max,
        };

        struct RouteVisibilityState
        {
            SightArguments Sight;
            float Spacing = 1.0f;
            Combine Reduction = Combine::Any;
        };

        CandidatePrepared PrepareRouteVisibility(const JsonValue& arguments, const CandidatePrepareContext& context,
                                                 std::string_view subject, std::vector<std::string>& errors)
        {
            RouteVisibilityState state;
            state.Sight = ReadSightArguments(arguments, ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0));
            state.Spacing = FloatOr(arguments, "spacing", 1.0f);
            state.Reduction = StringOr(arguments, "reduce", "any") == "max" ? Combine::Max : Combine::Any;
            if (!(state.Spacing > 0.0f))
                errors.push_back(std::string(subject) + ": spacing is positive");
            return Prepared(state);
        }

        // The start, every corner, and points along each leg at the spacing.
        // A route's corners begin at its first turn, so the start is added.
        template<typename Visit>
        bool ForEachSample(const Vec3d& start, std::span<const Vec3d> corners, float spacing, Visit&& visit)
        {
            Vec3d from = start;
            if (!visit(from))
                return false;
            for (const Vec3d& corner : corners)
            {
                const Vec3d leg = corner - from;
                const float length = leg.Magnitude();
                const int steps = static_cast<int>(std::floor(length / spacing));
                for (int step = 1; step < steps; ++step)
                {
                    if (!visit(from + leg * (static_cast<float>(step) * spacing / length)))
                        return false;
                }
                if (length > 0.0f && !visit(corner))
                    return false;
                from = corner;
            }
            return true;
        }

        void MeasureRouteVisibility(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                                    std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& route = *static_cast<const RouteVisibilityState*>(state);
            const SightArguments& sight = route.Sight;
            const std::uint32_t observers = run.SlotSize(sight.Slot);
            for (const std::uint32_t row : rows)
            {
                NavLocation target;
                const CandidateMeasureStatus located = run.CandidateNav(row, target);
                if (run.Physics() == nullptr || observers == 0 || located != CandidateMeasureStatus::Ok)
                {
                    statuses[row] = located != CandidateMeasureStatus::Ok ? located : CandidateMeasureStatus::NotApplicable;
                    continue;
                }
                const std::optional<NavLocation> start = run.QuerierNav();
                if (!start)
                {
                    statuses[row] = CandidateMeasureStatus::Failed;
                    continue;
                }
                if (!run.ChargeExactNavSearch())
                {
                    statuses[row] = CandidateMeasureStatus::BeyondBudget;
                    continue;
                }
                const NavStatus found =
                    NavFindRoute(*run.Navigation(), run.NavContext(), run.NavRequest(), *start, target, run.Route());
                if (found != NavStatus::Success)
                {
                    statuses[row] = found == NavStatus::NoPath ? CandidateMeasureStatus::Unreachable
                                                               : CandidateMeasureStatus::Failed;
                    continue;
                }

                bool spent = false;
                float best = 0.0f;
                for (std::uint32_t index = 0; index < observers && !spent; ++index)
                {
                    const CandidatePoint point = run.SlotPoint(sight.Slot, index);
                    const SightObserver observer{ .Eye = point.Position + kUp * sight.EyeHeight,
                                                  .Forward = point.Forward,
                                                  .HalfAngle = sight.HalfAngle,
                                                  .Range = sight.Range };
                    std::uint32_t samples = 0;
                    std::uint32_t seen = 0;
                    const bool finished = ForEachSample(start->Position, run.Route().Corners(), route.Spacing, [&](const Vec3d& sample) {
                        ++samples;
                        const std::optional<SightResult> result =
                            TestSightCharged(run, observer, sample + kUp * sight.TargetHeight, run.Querier(), point.Entity);
                        if (!result)
                        {
                            spent = true;
                            return false;
                        }
                        if (!result->Seen())
                            return true;
                        if (seen++ == 0 && run.TracingDetail())
                            run.RecordDetail(row, CandidateDetail{ sample, point.Entity, samples - 1 });
                        return route.Reduction == Combine::Max;
                    });
                    if (spent)
                        break;
                    const float share = samples > 0 ? static_cast<float>(seen) / static_cast<float>(samples) : 0.0f;
                    best = std::max(best, route.Reduction == Combine::Any ? (seen > 0 ? 1.0f : 0.0f) : share);
                    if (!finished && route.Reduction == Combine::Any)
                        break;
                }
                statuses[row] = spent ? CandidateMeasureStatus::BeyondBudget : CandidateMeasureStatus::Ok;
                values[row] = best;
            }
        }
    }

    CandidateMeasureDefinition RouteVisibilityMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.route_visibility";
        definition.DisplayName = "Route visibility";
        definition.Description = "Whether a slot's observers see the querier's route to the candidate, sampled "
                                 "at the spacing along each leg. One exact search per candidate plus one ray per "
                                 "sample inside the cone, so it runs last.";
        definition.Arguments = Record({ SlotField("slot", "Observers"),
                                        FloatField("spacing", "Sample spacing", 1.0, 0.0),
                                        EnumField("reduce", "Reduce", { "any", "max" }, "any") });
        for (DataFieldSchema& field : SightFields())
            definition.Arguments.Children.push_back(std::move(field));
        definition.Arguments.Children[4].Required = false;
        definition.Cost = CandidateCostClass::NavigationExact;
        definition.Value = CandidateValueKind::UnitInterval;
        definition.Prepare = PrepareRouteVisibility;
        definition.Measure = MeasureRouteVisibility;
        return definition;
    }
}
