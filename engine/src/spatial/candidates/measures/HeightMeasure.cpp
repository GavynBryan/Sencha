#include "../CandidateOperationSupport.h"

#include <algorithm>

namespace candidate_operation
{
    namespace
    {
        struct HeightState
        {
            std::uint8_t Slot = 0;
            Reduce Reduction = Reduce::Min;
        };

        CandidatePrepared PrepareHeight(const JsonValue& arguments, const CandidatePrepareContext& context,
                                        std::string_view subject, std::vector<std::string>& errors)
        {
            return Prepared(HeightState{ ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0),
                                         ReadReduce(arguments) });
        }

        void MeasureHeight(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                           std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& height = *static_cast<const HeightState*>(state);
            const std::uint32_t points = run.SlotSize(height.Slot);
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
                    const float value = run.Position(row).Y - run.SlotPoint(height.Slot, index).Position.Y;
                    reduced = index == 0 ? value
                        : height.Reduction == Reduce::Min ? std::min(reduced, value)
                                                          : std::max(reduced, value);
                }
                values[row] = reduced;
                statuses[row] = CandidateMeasureStatus::Ok;
            }
        }
    }

    CandidateMeasureDefinition HeightMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.height";
        definition.DisplayName = "Height";
        definition.Description = "Candidate height minus a slot point's height, in metres (signed).";
        definition.Arguments = Record({ SlotField("slot", "Slot"), ReduceField() });
        definition.Cost = CandidateCostClass::Geometric;
        definition.Prepare = PrepareHeight;
        definition.Measure = MeasureHeight;
        return definition;
    }
}
