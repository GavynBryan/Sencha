#include "../CandidateOperationSupport.h"

#include <math/Angle.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace candidate_operation
{
    namespace
    {
        // Others weighed in one run; more are left out.
        constexpr std::uint32_t kMostOthers = 32;

        struct SeparationState
        {
            std::uint8_t Others = 0;
            std::uint8_t Origin = 0;
            float Radius = 2.0f;
        };

        CandidatePrepared PrepareSeparation(const JsonValue& arguments, const CandidatePrepareContext& context,
                                            std::string_view subject, std::vector<std::string>& errors)
        {
            SeparationState state;
            state.Others = ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0);
            state.Origin = ReadSlot(arguments, "origin", "querier", context, subject, errors).value_or(0);
            state.Radius = FloatOr(arguments, "radius", 2.0f);
            if (!(state.Radius > 0.0f))
                errors.push_back(std::string(subject) + ": radius is positive");
            return Prepared(state);
        }

        // The direction from `origin` that `point` lies in, on the level; nullopt
        // within `radius`, where it lies in no direction yet.
        [[nodiscard]] std::optional<Vec3d> BearingFrom(const Vec3d& origin, const Vec3d& point, float radius)
        {
            const Vec3d toward(point.X - origin.X, 0.0f, point.Z - origin.Z);
            const float length = toward.Magnitude();
            if (length < radius)
                return std::nullopt;
            return toward / length;
        }

        void MeasureSeparation(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                               std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& separation = *static_cast<const SeparationState*>(state);
            std::array<Vec3d, kMostOthers> bearings{};
            std::uint32_t counted = 0;
            const bool hasOrigin = run.SlotSize(separation.Origin) != 0;
            const Vec3d origin = hasOrigin ? run.SlotPoint(separation.Origin, 0).Position : Vec3d::Zero();
            const std::uint32_t others = hasOrigin ? std::min(run.SlotSize(separation.Others), kMostOthers) : 0u;
            for (std::uint32_t index = 0; index < others; ++index)
                if (const std::optional<Vec3d> bearing =
                        BearingFrom(origin, run.SlotPoint(separation.Others, index).Position, separation.Radius))
                    bearings[counted++] = *bearing;

            for (const std::uint32_t row : rows)
            {
                const std::optional<Vec3d> bearing = BearingFrom(origin, run.Position(row), separation.Radius);
                if (counted == 0 || !bearing)
                {
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                    continue;
                }
                float nearest = -1.0f;
                for (std::uint32_t index = 0; index < counted; ++index)
                    nearest = std::max(nearest, bearings[index].Dot(*bearing));
                statuses[row] = CandidateMeasureStatus::Ok;
                values[row] = ToDegrees(std::acos(std::clamp(nearest, -1.0f, 1.0f)));
            }
        }
    }

    CandidateMeasureDefinition SeparationMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.separation";
        definition.DisplayName = "Separation";
        definition.Description = "Degrees round an origin from the candidate to the nearest of a slot's points, 0 to "
                                 "180. Scored rising, it spreads any number evenly round the origin. Within the radius "
                                 "of the origin a candidate is unmeasured and a point is not counted; with no point "
                                 "counted every candidate is unmeasured.";
        definition.Arguments = Record({ SlotField("slot", "Others"), SlotField("origin", "Origin", "querier"),
                                        FloatField("radius", "Radius", 2.0, 0.0) });
        definition.Cost = CandidateCostClass::Geometric;
        definition.AppliesTo = CandidateAppliesTo::Any;
        definition.Prepare = PrepareSeparation;
        definition.Measure = MeasureSeparation;
        return definition;
    }
}
