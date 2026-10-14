#include "SightMeasureSupport.h"

namespace candidate_operation
{
    namespace
    {
        enum class Observer : std::uint8_t
        {
            // Can the slot's points see the candidate?
            Slot,
            // Can the candidate see the slot's points?
            Candidate,
        };

        enum class Combine : std::uint8_t
        {
            Any,
            All,
            Fraction,
        };

        struct VisibilityState
        {
            SightArguments Sight;
            Observer Watcher = Observer::Slot;
            Combine Reduction = Combine::Any;
        };

        CandidatePrepared PrepareVisibility(const JsonValue& arguments, const CandidatePrepareContext& context,
                                            std::string_view subject, std::vector<std::string>& errors)
        {
            VisibilityState state;
            state.Sight = ReadSightArguments(arguments, ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0));
            state.Watcher = StringOr(arguments, "observer", "slot") == "candidate" ? Observer::Candidate : Observer::Slot;
            const std::string_view reduce = StringOr(arguments, "reduce", "any");
            state.Reduction = reduce == "all" ? Combine::All : reduce == "fraction" ? Combine::Fraction : Combine::Any;
            return Prepared(state);
        }

        void MeasureVisibility(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                               std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& visibility = *static_cast<const VisibilityState*>(state);
            const SightArguments& sight = visibility.Sight;
            const std::uint32_t points = run.SlotSize(sight.Slot);
            for (const std::uint32_t row : rows)
            {
                if (run.Physics() == nullptr || points == 0)
                {
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                    continue;
                }
                std::uint32_t seen = 0;
                bool spent = false;
                bool recorded = false;
                for (std::uint32_t index = 0; index < points && !spent; ++index)
                {
                    const CandidatePoint point = run.SlotPoint(sight.Slot, index);
                    const bool slotWatches = visibility.Watcher == Observer::Slot;
                    const Vec3d& eyeBase = slotWatches ? point.Position : run.Position(row);
                    const Vec3d& targetBase = slotWatches ? run.Position(row) : point.Position;
                    const SightObserver observer{ .Eye = eyeBase + kUp * sight.EyeHeight,
                                                  .Forward = slotWatches ? point.Forward : Vec3d::Zero(),
                                                  .HalfAngle = sight.HalfAngle,
                                                  .Range = sight.Range };
                    const EntityId observerEntity = slotWatches ? point.Entity : run.Entity(row);
                    const EntityId targetEntity = slotWatches ? run.Entity(row) : point.Entity;
                    const std::optional<SightResult> result = TestSightCharged(
                        run, observer, targetBase + kUp * sight.TargetHeight, targetEntity, observerEntity);
                    if (!result)
                    {
                        spent = true;
                        break;
                    }
                    if (result->Seen())
                        ++seen;
                    else if (!recorded && run.TracingDetail())
                    {
                        run.RecordDetail(row, CandidateDetail{ result->HitPoint, result->HitEntity,
                                                               static_cast<std::uint32_t>(result->Outcome) });
                        recorded = true;
                    }
                }
                if (spent)
                {
                    statuses[row] = CandidateMeasureStatus::BeyondBudget;
                    continue;
                }
                statuses[row] = CandidateMeasureStatus::Ok;
                switch (visibility.Reduction)
                {
                case Combine::Any: values[row] = seen > 0 ? 1.0f : 0.0f; break;
                case Combine::All: values[row] = seen == points ? 1.0f : 0.0f; break;
                case Combine::Fraction: values[row] = static_cast<float>(seen) / static_cast<float>(points); break;
                }
            }
        }
    }

    CandidateMeasureDefinition VisibilityMeasure()
    {
        CandidateMeasureDefinition definition;
        definition.Name = "candidates.measure.visibility";
        definition.DisplayName = "Visibility";
        definition.Description = "Whether a sight line joins the candidate and a slot's points, within the "
                                 "observer's view cone and range. The observer is the slot (can they see this "
                                 "spot?) or the candidate (can I see them from here?).";
        definition.Arguments = Record({ SlotField("slot", "Slot"),
                                        EnumField("observer", "Observer", { "slot", "candidate" }, "slot"),
                                        EnumField("reduce", "Over the slot's points", { "any", "all", "fraction" }, "any") });
        for (DataFieldSchema& field : SightFields())
            definition.Arguments.Children.push_back(std::move(field));
        definition.Arguments.Children[4].Required = false;
        definition.Cost = CandidateCostClass::Physics;
        definition.Value = CandidateValueKind::UnitInterval;
        definition.Prepare = PrepareVisibility;
        definition.Measure = MeasureVisibility;
        return definition;
    }
}
