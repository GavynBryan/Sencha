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
            // Heights above the candidate's point, each tested as its own
            // sample; empty tests the one authored height.
            std::vector<float> SampleHeights;
        };

        CandidatePrepared PrepareVisibility(const JsonValue& arguments, const CandidatePrepareContext& context,
                                            std::string_view subject, std::vector<std::string>& errors)
        {
            VisibilityState state;
            state.Sight = ReadSightArguments(arguments, ReadSlot(arguments, "slot", {}, context, subject, errors).value_or(0));
            state.Watcher = StringOr(arguments, "observer", "slot") == "candidate" ? Observer::Candidate : Observer::Slot;
            const std::string_view reduce = StringOr(arguments, "reduce", "any");
            state.Reduction = reduce == "all" ? Combine::All : reduce == "fraction" ? Combine::Fraction : Combine::Any;
            if (const JsonValue* heights = arguments.Find("sample_heights"))
            {
                for (const JsonValue& height : heights->AsArray())
                    state.SampleHeights.push_back(static_cast<float>(height.AsNumber()));
            }
            return Prepared(state);
        }

        void MeasureVisibility(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                               std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& visibility = *static_cast<const VisibilityState*>(state);
            const std::uint32_t points = run.SlotSize(visibility.Sight.Slot);
            const bool slotWatches = visibility.Watcher == Observer::Slot;
            const std::size_t heights = std::max<std::size_t>(visibility.SampleHeights.size(), 1);
            const std::uint32_t samples = points * static_cast<std::uint32_t>(heights);
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
                for (std::size_t height = 0; height < heights && !spent; ++height)
                {
                    SightArguments sight = visibility.Sight;
                    if (!visibility.SampleHeights.empty())
                        (slotWatches ? sight.TargetHeight : sight.EyeHeight) = visibility.SampleHeights[height];
                    for (std::uint32_t index = 0; index < points; ++index)
                    {
                        const CandidatePoint point = run.SlotPoint(sight.Slot, index);
                        const Vec3d& eyeBase = slotWatches ? point.Position : run.Position(row);
                        const Vec3d& targetBase = slotWatches ? run.Position(row) : point.Position;
                        const EntityId observerEntity = slotWatches ? point.Entity : run.Entity(row);
                        const EntityId targetEntity = slotWatches ? run.Entity(row) : point.Entity;
                        const std::optional<SightObserver> observer =
                            ObserverAt(run, observerEntity, eyeBase, slotWatches ? point.Forward : Vec3d::Zero(), sight);
                        if (!observer)
                            continue;
                        const std::optional<SightResult> result = TestSightCharged(
                            run, *observer, TargetAt(run, targetEntity, targetBase, sight), targetEntity, observerEntity);
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
                case Combine::All: values[row] = seen == samples ? 1.0f : 0.0f; break;
                case Combine::Fraction: values[row] = static_cast<float>(seen) / static_cast<float>(samples); break;
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
                                 "spot?) or the candidate (can I see them from here?). Sample heights test the "
                                 "candidate's side at each height, so a fraction tells low cover from open ground.";
        definition.Arguments = Record({ SlotField("slot", "Slot"),
                                        EnumField("observer", "Observer", { "slot", "candidate" }, "slot"),
                                        EnumField("reduce", "Over the slot's points", { "any", "all", "fraction" }, "any") });
        for (DataFieldSchema& field : SightFields())
            definition.Arguments.Children.push_back(std::move(field));
        definition.Arguments.Children[4].Required = false;
        DataFieldSchema heights = MakeDataField(DataFieldKind::Array, "sample_heights", "Sample heights");
        heights.Required = false;
        heights.Children.push_back(MakeDataField(DataFieldKind::Float, "height", "Height"));
        definition.Arguments.Children.push_back(std::move(heights));
        definition.Cost = CandidateCostClass::Physics;
        definition.Value = CandidateValueKind::UnitInterval;
        definition.Prepare = PrepareVisibility;
        definition.Measure = MeasureVisibility;
        return definition;
    }
}
