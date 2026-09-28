#include "../CandidateOperationSupport.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace candidate_operation
{
    namespace
    {
        struct FacingState
        {
            std::uint8_t Slot = 0;
            Reduce Reduction = Reduce::Min;
        };

        CandidatePrepared PrepareFacing(const JsonValue& arguments, const CandidatePrepareContext& context,
                                        std::string_view subject, std::vector<std::string>& errors)
        {
            return Prepared(FacingState{ ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0),
                                         ReadReduce(arguments) });
        }

        // Horizontal angle between where the point faces and where the candidate
        // lies from it: 0 in front, 180 behind. Nullopt without a facing.
        [[nodiscard]] std::optional<float> FacingAngle(const CandidatePoint& point, const Vec3d& candidate)
        {
            const Vec3d forward(point.Forward.X, 0.0f, point.Forward.Z);
            const Vec3d toward(candidate.X - point.Position.X, 0.0f, candidate.Z - point.Position.Z);
            const float lengths = forward.Magnitude() * toward.Magnitude();
            if (!(lengths > 0.0f))
                return std::nullopt;
            const float cosine = std::clamp(forward.Dot(toward) / lengths, -1.0f, 1.0f);
            return std::acos(cosine) * 180.0f / std::numbers::pi_v<float>;
        }

        void MeasureFacing(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                           std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& facing = *static_cast<const FacingState*>(state);
            const std::uint32_t points = run.SlotSize(facing.Slot);
            for (const std::uint32_t row : rows)
            {
                std::optional<float> reduced;
                for (std::uint32_t index = 0; index < points; ++index)
                {
                    const std::optional<float> angle = FacingAngle(run.SlotPoint(facing.Slot, index), run.Position(row));
                    if (!angle)
                        continue;
                    reduced = !reduced ? *angle
                        : facing.Reduction == Reduce::Min ? std::min(*reduced, *angle)
                                                          : std::max(*reduced, *angle);
                }
                statuses[row] = reduced ? CandidateMeasureStatus::Ok : CandidateMeasureStatus::NotApplicable;
                values[row] = reduced.value_or(0.0f);
            }
        }
    }

    CandidateMeasureDefinition FacingMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.facing";
        definition.DisplayName = "Facing";
        definition.Description = "Degrees between a slot point's forward and the direction to the candidate, "
                                 "measured horizontally: 0 in front, 180 behind.";
        definition.Arguments = Record({ SlotField("slot", "Slot"), ReduceField() });
        definition.Cost = CandidateCostClass::Geometric;
        definition.Prepare = PrepareFacing;
        definition.Measure = MeasureFacing;
        return definition;
    }
}
