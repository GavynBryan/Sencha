#include "../CandidateOperationSupport.h"

#include <algorithm>

namespace candidate_operation
{
    namespace
    {
        struct DistanceState
        {
            std::uint8_t Slot = 0;
            Reduce Reduction = Reduce::Min;
            bool Horizontal = false;
        };

        CandidatePrepared PrepareDistance(const JsonValue& arguments, const CandidatePrepareContext& context,
                                          std::string_view subject, std::vector<std::string>& errors)
        {
            DistanceState state;
            state.Slot = ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0);
            state.Reduction = ReadReduce(arguments);
            state.Horizontal = BoolOr(arguments, "horizontal", false);
            return Prepared(state);
        }

        void MeasureDistance(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                             std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& distance = *static_cast<const DistanceState*>(state);
            const std::uint32_t points = run.SlotSize(distance.Slot);
            for (const std::uint32_t row : rows)
            {
                if (points == 0)
                {
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                    continue;
                }
                float reduced = 0.0f;
                for (std::uint32_t index = 0; index < points; ++index)
                {
                    Vec3d offset = run.Position(row) - run.SlotPoint(distance.Slot, index).Position;
                    if (distance.Horizontal)
                        offset.Y = 0.0f;
                    const float value = offset.Magnitude();
                    reduced = index == 0 ? value
                        : distance.Reduction == Reduce::Min ? std::min(reduced, value)
                                                            : std::max(reduced, value);
                }
                values[row] = reduced;
                statuses[row] = CandidateMeasureStatus::Ok;
            }
        }
    }

    CandidateMeasureDefinition DistanceMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.distance";
        definition.DisplayName = "Distance";
        definition.Description = "Straight-line distance in metres to a slot's nearest (min) or farthest (max) point.";
        definition.Arguments = Record({ SlotField("slot", "Slot"), ReduceField(),
                                        BoolField("horizontal", "Horizontal only", false) });
        definition.Cost = CandidateCostClass::Geometric;
        definition.Prepare = PrepareDistance;
        definition.Measure = MeasureDistance;
        return definition;
    }
}
