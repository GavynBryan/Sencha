#include "../CandidateOperationSupport.h"

namespace candidate_operation
{
    namespace
    {
        struct PointsState
        {
            std::uint8_t Slot = 0;
        };

        CandidatePrepared PreparePoints(const JsonValue& arguments, const CandidatePrepareContext& context,
                                        std::string_view subject, std::vector<std::string>& errors)
        {
            const std::optional<std::uint8_t> slot = ReadSlot(arguments, "slot", {}, context, subject, errors);
            return Prepared(PointsState{ slot.value_or(0) });
        }

        void GeneratePoints(CandidateRun& run, const void* state)
        {
            const auto& points = *static_cast<const PointsState*>(state);
            for (std::uint32_t index = 0; index < run.SlotSize(points.Slot); ++index)
            {
                const CandidatePoint point = run.SlotPoint(points.Slot, index);
                if (!run.Append(point.Position, point.Entity))
                    return;
            }
        }
    }

    CandidateGeneratorDefinition PointsGenerator()
    {
        CandidateGeneratorDefinition definition;
        definition.Name = "candidates.generator.points";
        definition.DisplayName = "Slot points";
        definition.Description = "The points bound to one slot, in binding order. Points carrying an entity "
                                 "become entity candidates.";
        definition.Arguments = Record({ SlotField("slot", "Slot") });
        definition.Prepare = PreparePoints;
        definition.Generate = GeneratePoints;
        return definition;
    }
}
