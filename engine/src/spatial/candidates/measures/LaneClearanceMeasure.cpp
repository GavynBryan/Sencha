#include "../CandidateOperationSupport.h"

#include <algorithm>
#include <limits>

namespace candidate_operation
{
    namespace
    {
        struct LaneClearanceState
        {
            std::uint8_t Others = 0;
            std::uint8_t Target = 0;
            float Lift = 1.3f;
            float EndGap = 1.5f;
        };

        CandidatePrepared PrepareLaneClearance(const JsonValue& arguments, const CandidatePrepareContext& context,
                                               std::string_view subject, std::vector<std::string>& errors)
        {
            LaneClearanceState state;
            state.Others = ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0);
            state.Target = ReadSlot(arguments, "target", {}, context, subject, errors).value_or(0);
            state.Lift = FloatOr(arguments, "lift", 1.3f);
            state.EndGap = FloatOr(arguments, "end_gap", 1.5f);
            return Prepared(state);
        }

        void MeasureLaneClearance(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                                  std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& lane = *static_cast<const LaneClearanceState*>(state);
            const std::uint32_t others = run.SlotSize(lane.Others);
            if (others == 0 || run.SlotSize(lane.Target) == 0)
            {
                for (const std::uint32_t row : rows)
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                return;
            }
            const Vec3d target = run.SlotPoint(lane.Target, 0).Position;
            for (const std::uint32_t row : rows)
            {
                const Vec3d from = run.Position(row) + Vec3d::Up() * lane.Lift;
                const Vec3d toward = target - from;
                const float length = toward.Magnitude();
                if (length <= lane.EndGap)
                {
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                    continue;
                }
                const Vec3d direction = toward / length;
                const float reach = length - lane.EndGap;
                float clearest = std::numeric_limits<float>::max();
                for (std::uint32_t index = 0; index < others; ++index)
                {
                    const Vec3d offset = run.SlotPoint(lane.Others, index).Position - from;
                    const float along = std::clamp(offset.Dot(direction), 0.0f, reach);
                    clearest = std::min(clearest, (offset - direction * along).Magnitude());
                }
                statuses[row] = CandidateMeasureStatus::Ok;
                values[row] = clearest;
            }
        }
    }

    CandidateMeasureDefinition LaneClearanceMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.lane_clearance";
        definition.DisplayName = "Lane clearance";
        definition.Description = "Metres between a slot's points and the straight line from the candidate to a "
                                 "target, stopping short of the target. Unmeasured with no points.";
        definition.Arguments = Record({ SlotField("slot", "Points"), SlotField("target", "Target"),
                                        FloatField("lift", "Lift", 1.3, 0.0),
                                        FloatField("end_gap", "End gap", 1.5, 0.0) });
        definition.Cost = CandidateCostClass::Geometric;
        definition.AppliesTo = CandidateAppliesTo::Any;
        definition.Prepare = PrepareLaneClearance;
        definition.Measure = MeasureLaneClearance;
        return definition;
    }
}
