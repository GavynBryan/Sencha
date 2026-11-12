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

        enum class Sighting : std::uint8_t
        {
            Seen,
            Unseen,
            BudgetSpent,
        };

        // A slot point with no eye to see from counts as unseen. The first blocked
        // line is kept as the row's detail when the run traces it.
        Sighting SightBetween(CandidateRun& run, const SightArguments& sight, bool slotWatches, std::uint32_t row,
                              std::uint32_t index, bool& recorded)
        {
            const CandidatePoint point = run.SlotPoint(sight.Slot, index);
            const Vec3d& eyeBase = slotWatches ? point.Position : run.Position(row);
            const Vec3d& targetBase = slotWatches ? run.Position(row) : point.Position;
            const EntityId observerEntity = slotWatches ? point.Entity : run.Entity(row);
            const EntityId targetEntity = slotWatches ? run.Entity(row) : point.Entity;
            const std::optional<SightObserver> observer =
                ObserverAt(run, observerEntity, eyeBase, slotWatches ? point.Forward : Vec3d::Zero(), sight);
            if (!observer)
                return Sighting::Unseen;
            const std::optional<SightResult> result = TestSightCharged(
                run, *observer, TargetAt(run, targetEntity, targetBase, sight), targetEntity, observerEntity);
            if (!result)
                return Sighting::BudgetSpent;
            if (result->Seen())
                return Sighting::Seen;
            if (!recorded && run.TracingDetail())
            {
                run.RecordDetail(row, CandidateDetail{ result->HitPoint, result->HitEntity,
                                                       static_cast<std::uint32_t>(result->Outcome) });
                recorded = true;
            }
            return Sighting::Unseen;
        }

        // Over every slot point at every sample height; none once the budget is spent.
        std::optional<std::uint32_t> CountSeen(CandidateRun& run, const VisibilityState& visibility, std::uint32_t row,
                                               std::uint32_t points)
        {
            const bool slotWatches = visibility.Watcher == Observer::Slot;
            const std::size_t heights = std::max<std::size_t>(visibility.SampleHeights.size(), 1);
            std::uint32_t seen = 0;
            bool recorded = false;
            for (std::size_t height = 0; height < heights; ++height)
            {
                SightArguments sight = visibility.Sight;
                if (!visibility.SampleHeights.empty())
                    (slotWatches ? sight.TargetHeight : sight.EyeHeight) = visibility.SampleHeights[height];
                for (std::uint32_t index = 0; index < points; ++index)
                {
                    const Sighting sighting = SightBetween(run, sight, slotWatches, row, index, recorded);
                    if (sighting == Sighting::BudgetSpent)
                        return std::nullopt;
                    seen += sighting == Sighting::Seen ? 1u : 0u;
                }
            }
            return seen;
        }

        float Reduce(Combine reduction, std::uint32_t seen, std::uint32_t samples)
        {
            switch (reduction)
            {
            case Combine::All: return seen == samples ? 1.0f : 0.0f;
            case Combine::Fraction: return static_cast<float>(seen) / static_cast<float>(samples);
            case Combine::Any: break;
            }
            return seen > 0 ? 1.0f : 0.0f;
        }

        void MeasureVisibility(CandidateRun& run, const void* state, std::span<const std::uint32_t> rows,
                               std::span<float> values, std::span<CandidateMeasureStatus> statuses)
        {
            const auto& visibility = *static_cast<const VisibilityState*>(state);
            const std::uint32_t points = run.SlotSize(visibility.Sight.Slot);
            const std::size_t heights = std::max<std::size_t>(visibility.SampleHeights.size(), 1);
            const std::uint32_t samples = points * static_cast<std::uint32_t>(heights);
            for (const std::uint32_t row : rows)
            {
                if (run.Physics() == nullptr || points == 0)
                {
                    statuses[row] = CandidateMeasureStatus::NotApplicable;
                    continue;
                }
                const std::optional<std::uint32_t> seen = CountSeen(run, visibility, row, points);
                statuses[row] = seen ? CandidateMeasureStatus::Ok : CandidateMeasureStatus::BeyondBudget;
                if (seen)
                    values[row] = Reduce(visibility.Reduction, *seen, samples);
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
