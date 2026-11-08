#include "NavigationMeasureSupport.h"

#include <math/Angle.h>

#include <array>
#include <cmath>

namespace candidate_operation
{
    namespace
    {
        // Others weighed in one run; more are left out, counted in neither direction.
        constexpr std::uint32_t kMostOthers = 32;

        enum class Bearing : std::uint8_t
        {
            Straight,
            Route,
        };

        struct CrowdingState
        {
            std::uint8_t Others = 0;
            std::uint8_t Origin = 0;
            bool ByDistance = false;
            float AngleDegrees = 30.0f;
            float Distance = 4.0f;
            Bearing Way = Bearing::Straight;
            float Radius = 6.0f;
        };

        CandidatePrepared PrepareCrowding(const JsonValue& arguments, const CandidatePrepareContext& context,
                                          std::string_view subject, std::vector<std::string>& errors)
        {
            CrowdingState state;
            state.Others = ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0);
            state.ByDistance = StringOr(arguments, "mode", "angle") == "distance";
            state.Origin = ReadSlot(arguments, "origin", "querier", context, subject, errors).value_or(0);
            state.AngleDegrees = FloatOr(arguments, "angle", 30.0f);
            state.Distance = FloatOr(arguments, "distance", 4.0f);
            state.Way = StringOr(arguments, "bearing", "straight") == "route" ? Bearing::Route : Bearing::Straight;
            state.Radius = FloatOr(arguments, "radius", 6.0f);
            if (!(state.AngleDegrees > 0.0f) || !(state.Distance > 0.0f) || !(state.Radius > 0.0f))
                errors.push_back(std::string(subject) + ": angle, distance and radius are positive");
            return Prepared(state);
        }

        [[nodiscard]] Vec3d Flat(const Vec3d& v) { return Vec3d(v.X, 0.0f, v.Z); }

        // Where the route from `origin` to `to` leaves `radius` of it, or the
        // route's end when it never does.
        [[nodiscard]] Vec3d RouteExit(const Vec3d& origin, std::span<const Vec3d> corners, float radius)
        {
            Vec3d from = origin;
            for (const Vec3d& corner : corners)
            {
                if (Flat(corner - origin).Magnitude() >= radius)
                {
                    // Along the leg to where it crosses the radius, near enough for a bearing.
                    const Vec3d leg = corner - from;
                    const float length = leg.Magnitude();
                    for (float step = 0.0f; length > 0.0f && step < length; step += 0.25f)
                    {
                        const Vec3d point = from + leg * (step / length);
                        if (Flat(point - origin).Magnitude() >= radius)
                            return point;
                    }
                    return corner;
                }
                from = corner;
            }
            return from;
        }

        // The direction from `origin` that `to` lies in, straight or by route;
        // nullopt when there is none to take.
        [[nodiscard]] std::optional<Vec3d> BearingOf(CandidateRun& run, const CrowdingState& crowding,
                                                     const Vec3d& origin, const std::optional<NavLocation>& start,
                                                     const Vec3d& to, std::optional<NavLocation> end,
                                                     CandidateMeasureStatus& status)
        {
            Vec3d toward = Flat(to - origin);
            if (crowding.Way == Bearing::Route)
            {
                if (!start || !end)
                {
                    status = CandidateMeasureStatus::OffNavigation;
                    return std::nullopt;
                }
                if (!run.ChargeExactNavSearch())
                {
                    status = CandidateMeasureStatus::BeyondBudget;
                    return std::nullopt;
                }
                const NavStatus found =
                    NavFindRoute(*run.Navigation(), run.NavContext(), run.NavRequest(), *start, *end, run.Route());
                if (found != NavStatus::Success)
                {
                    status = found == NavStatus::NoPath ? CandidateMeasureStatus::Unreachable
                                                        : CandidateMeasureStatus::Failed;
                    return std::nullopt;
                }
                toward = Flat(RouteExit(start->Position, run.Route().Corners(), crowding.Radius) - start->Position);
            }
            const float length = toward.Magnitude();
            if (!(length > 1.0e-4f))
                return std::nullopt;
            return toward / length;
        }

        void MeasureCrowding(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                             std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& crowding = *static_cast<const CrowdingState*>(state);
            const std::uint32_t others = std::min(run.SlotSize(crowding.Others), kMostOthers);
            if (crowding.ByDistance)
            {
                for (const std::uint32_t row : rows)
                {
                    std::uint32_t near = 0;
                    for (std::uint32_t index = 0; index < others; ++index)
                        if ((run.SlotPoint(crowding.Others, index).Position - run.Position(row)).Magnitude()
                            <= crowding.Distance)
                            ++near;
                    statuses[row] = CandidateMeasureStatus::Ok;
                    values[row] = static_cast<float>(near);
                }
                return;
            }

            if (run.SlotSize(crowding.Origin) == 0)
            {
                for (const std::uint32_t row : rows)
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                return;
            }
            const Vec3d origin = run.SlotPoint(crowding.Origin, 0).Position;
            const bool byRoute = crowding.Way == Bearing::Route;
            const std::optional<NavLocation> start = byRoute ? run.ProjectNav(origin) : std::nullopt;
            std::array<std::optional<Vec3d>, kMostOthers> bearings{};
            for (std::uint32_t index = 0; index < others; ++index)
            {
                const Vec3d other = run.SlotPoint(crowding.Others, index).Position;
                CandidateMeasureStatus ignored = CandidateMeasureStatus::Ok;
                bearings[index] =
                    BearingOf(run, crowding, origin, start, other, byRoute ? run.ProjectNav(other) : std::nullopt, ignored);
            }

            const float cosine = std::cos(ToRadians(crowding.AngleDegrees));
            for (const std::uint32_t row : rows)
            {
                std::optional<NavLocation> end;
                if (byRoute)
                {
                    NavLocation located;
                    if (run.CandidateNav(row, located) == CandidateMeasureStatus::Ok)
                        end = located;
                }
                CandidateMeasureStatus status = CandidateMeasureStatus::Ok;
                const std::optional<Vec3d> bearing = BearingOf(run, crowding, origin, start, run.Position(row), end, status);
                if (!bearing)
                {
                    statuses[row] = status == CandidateMeasureStatus::Ok ? CandidateMeasureStatus::NotApplicable : status;
                    continue;
                }
                std::uint32_t crowded = 0;
                for (std::uint32_t index = 0; index < others; ++index)
                    if (bearings[index] && bearings[index]->Dot(*bearing) >= cosine)
                        ++crowded;
                statuses[row] = CandidateMeasureStatus::Ok;
                values[row] = static_cast<float>(crowded);
            }
        }
    }

    CandidateMeasureDefinition CrowdingMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.crowding";
        definition.DisplayName = "Crowding";
        definition.Description = "How many of a slot's points share the candidate's direction from an origin: within "
                                 "an angle of its bearing, straight or where the route from the origin leaves a "
                                 "radius, or within a distance of it. The route form charges an exact search per "
                                 "candidate and per point.";
        definition.Arguments = Record({ SlotField("slot", "Others"), SlotField("origin", "Origin", "querier"),
                                        EnumField("mode", "Mode", { "angle", "distance" }, "angle"),
                                        FloatField("angle", "Angle (degrees)", 30.0, 0.0),
                                        FloatField("distance", "Distance", 4.0, 0.0),
                                        EnumField("bearing", "Bearing", { "straight", "route" }, "straight"),
                                        FloatField("radius", "Route radius", 6.0, 0.0) });
        definition.Cost = CandidateCostClass::NavigationExact;
        definition.AppliesTo = CandidateAppliesTo::Any;
        definition.Prepare = PrepareCrowding;
        definition.Measure = MeasureCrowding;
        return definition;
    }
}
