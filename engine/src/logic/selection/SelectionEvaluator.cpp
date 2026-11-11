#include <logic/selection/SelectionEvaluator.h>

#include <authored/AuthoredQueryDispatcher.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <math/SeededPick.h>

#include <algorithm>

namespace
{
    [[nodiscard]] float NumericValue(const AuthoredValue& answer)
    {
        bool flag = false;
        std::int64_t whole = 0;
        double number = 0.0;
        if (answer.TryGetBool(flag))
            return flag ? 1.0f : 0.0f;
        if (answer.TryGetInt(whole))
            return static_cast<float>(whole);
        (void)answer.TryGetFloat(number);
        return static_cast<float>(number);
    }

    [[nodiscard]] bool ContextFits(const SelectionTable& table, const SelectionContext& context)
    {
        return !context.Entities.empty() && context.Entities.size() >= table.Entities().size()
            && context.Inputs.size() >= table.Inputs().size()
            && (!context.Current || *context.Current < table.Options().size());
    }

    // One option's line of the trace, when there is a trace.
    class OptionTraceWriter
    {
    public:
        OptionTraceWriter(SelectionTrace* trace, std::size_t considerations)
            : Trace(trace)
        {
            if (Trace == nullptr)
                return;
            Option = &Trace->Options.emplace_back();
            Option->FirstValue = static_cast<std::uint32_t>(Trace->Values.size());
            Trace->Values.resize(Trace->Values.size() + considerations);
        }

        void Value(std::uint32_t index, std::optional<float> value, float output) const
        {
            if (Option != nullptr)
                Trace->Values[Option->FirstValue + index] = {
                    value.value_or(0.0f), output,
                    value ? SelectionValueStatus::Measured : SelectionValueStatus::Unmeasured };
        }

        void Rejected(std::uint32_t index) const
        {
            if (Option != nullptr)
                Option->RejectedBy = index;
        }

        void Unavailable(bool unavailable) const
        {
            if (Option != nullptr)
                Option->Unavailable = unavailable;
        }

        void Finished(bool qualified, float score) const
        {
            if (Option == nullptr)
                return;
            Option->Qualified = qualified;
            Option->Score = score;
        }

    private:
        SelectionTrace* Trace = nullptr;
        SelectionTraceOption* Option = nullptr;
    };

    template <typename MeasureFn>
    [[nodiscard]] bool MeetsRequirements(std::span<const SelectionConsideration> considerations, MeasureFn&& measure,
                                         const OptionTraceWriter& writer)
    {
        for (std::uint32_t index = 0; index < considerations.size(); ++index)
        {
            const ConsiderationRule& rule = considerations[index].Rule;
            if (rule.Mode != ConsiderationMode::Require)
                continue;
            const std::optional<float> value = measure(considerations[index]);
            const bool passes = value ? rule.Admits(*value) : rule.UnmeasuredScore.has_value();
            writer.Value(index, value, passes ? 1.0f : 0.0f);
            if (!passes)
            {
                writer.Rejected(index);
                return false;
            }
        }
        return true;
    }

    // The weighted mean of the scored considerations, or none when one is
    // unmeasured with nothing to score it as.
    template <typename MeasureFn>
    [[nodiscard]] std::optional<float> WeightedScore(std::span<const SelectionConsideration> considerations,
                                                     MeasureFn&& measure, const OptionTraceWriter& writer)
    {
        float sum = 0.0f;
        float weights = 0.0f;
        for (std::uint32_t index = 0; index < considerations.size(); ++index)
        {
            const ConsiderationRule& rule = considerations[index].Rule;
            if (rule.Mode != ConsiderationMode::Score)
                continue;
            const std::optional<float> value = measure(considerations[index]);
            if (!value && !rule.UnmeasuredScore)
            {
                writer.Value(index, std::nullopt, 0.0f);
                writer.Rejected(index);
                return std::nullopt;
            }
            const float output = value ? rule.Output(*value) : *rule.UnmeasuredScore;
            writer.Value(index, value, output);
            sum += rule.Weight * output;
            weights += rule.Weight;
        }
        return weights > 0.0f ? sum / weights : 1.0f;
    }

    [[nodiscard]] std::optional<std::uint32_t> BestQualified(const SelectionScratch& scratch)
    {
        std::optional<std::uint32_t> best;
        for (std::uint32_t index = 0; index < scratch.Scores.size(); ++index)
            if (scratch.Qualified[index] != 0 && (!best || scratch.Scores[index] > scratch.Scores[*best]))
                best = index;
        return best;
    }

    [[nodiscard]] std::uint32_t PickFromBand(const SelectionScratch& scratch, std::uint32_t best, float width,
                                             std::uint64_t seed)
    {
        const float floor = scratch.Scores[best] - width;
        SeededBandPick band(seed);
        std::uint32_t pick = best;
        for (std::uint32_t index = 0; index < scratch.Scores.size(); ++index)
            if (scratch.Qualified[index] != 0 && scratch.Scores[index] >= floor && band.Offer(index))
                pick = index;
        return pick;
    }

    [[nodiscard]] SelectionResult Decide(const SelectionTable& table, const SelectionContext& context,
                                         const SelectionScratch& scratch, bool interrupted)
    {
        SelectionResult result;
        result.Interrupted = interrupted;
        const auto finish = [&](std::uint32_t option, SelectionDecision decision) {
            result.Status = SelectionStatus::Success;
            result.Option = option;
            result.Score = scratch.Scores[option];
            result.Decision = decision;
        };
        const bool currentQualifies = context.Current && scratch.Qualified[*context.Current] != 0;
        const std::optional<std::uint32_t> best = BestQualified(scratch);
        if (currentQualifies && !interrupted
            && context.CurrentSeconds < table.Options()[*context.Current].CommitSeconds)
            finish(*context.Current, SelectionDecision::Committed);
        else if (!best)
            result.Status = SelectionStatus::NoneQualified;
        else if (currentQualifies && *best != *context.Current
                 && scratch.Scores[*best] < scratch.Scores[*context.Current] + table.SwitchMargin())
            finish(*context.Current, SelectionDecision::WithinMargin);
        else if (table.Mode() == SelectionMode::PickFromBand)
            finish(PickFromBand(scratch, *best, table.BandWidth(), context.Seed), SelectionDecision::Chosen);
        else
            finish(*best, SelectionDecision::Chosen);
        return result;
    }
}

SelectionEvaluator::SelectionEvaluator(const World& world, const AuthoredQueryDispatcher* queries)
    : Entities(world)
    , Queries(queries)
{
}

std::optional<float> SelectionEvaluator::Measure(const SelectionConsideration& consideration,
                                                 const SelectionContext& context, SelectionScratch& scratch) const
{
    if (consideration.Source == SelectionSource::Input)
        return context.Inputs[consideration.Input];
    if (Queries == nullptr)
        return std::nullopt;

    scratch.Arguments.Resize(consideration.Call.Arguments.Size());
    for (std::size_t slot = 0; slot < consideration.Call.Arguments.Size(); ++slot)
        scratch.Arguments.Set(slot, consideration.Call.Arguments.At(slot));
    for (std::size_t index = 0; index < consideration.EntitySlots.size(); ++index)
    {
        const EntityId entity = context.Entities[consideration.Entities[index]];
        if (!entity.IsValid())
            return std::nullopt;
        scratch.Arguments.Set(consideration.EntitySlots[index], AuthoredValue::Entity(entity));
    }
    if (Queries->Evaluate(consideration.Call.Query, scratch.Arguments.Values(), scratch.Answer)
        != AuthoredQueryStatus::Value)
        return std::nullopt;
    return NumericValue(scratch.Answer);
}

bool SelectionEvaluator::Interrupted(const SelectionTable& table, EntityId querier) const
{
    const GameplayTagQuery* interrupt = table.Interrupt();
    if (interrupt == nullptr)
        return false;
    const GameplayTagContainer* tags = Entities.TryGet<GameplayTagContainer>(querier);
    const GameplayTagRegistry* registry = Entities.TryGetResource<GameplayTagRegistry>();
    return tags != nullptr && registry != nullptr && interrupt->Matches(*tags, *registry);
}

SelectionResult SelectionEvaluator::Evaluate(const SelectionTable& table, const SelectionContext& context,
                                             SelectionScratch& scratch, SelectionTrace* trace) const
{
    if (trace != nullptr)
    {
        trace->Options.clear();
        trace->Values.clear();
    }
    SelectionResult result;
    if (ContextFits(table, context))
    {
        const auto measure = [&](const SelectionConsideration& consideration) {
            return Measure(consideration, context, scratch);
        };
        const std::span<const SelectionOption> options = table.Options();
        scratch.Scores.assign(options.size(), 0.0f);
        scratch.Qualified.assign(options.size(), 0);
        for (std::uint32_t index = 0; index < options.size(); ++index)
        {
            const std::span<const SelectionConsideration> considerations = options[index].Considerations;
            const OptionTraceWriter writer(trace, considerations.size());
            const bool unavailable = index < 64 && (context.Unavailable >> index & 1u) != 0;
            writer.Unavailable(unavailable);
            const std::optional<float> score = !unavailable && MeetsRequirements(considerations, measure, writer)
                ? WeightedScore(considerations, measure, writer)
                : std::nullopt;
            scratch.Qualified[index] = score ? 1 : 0;
            scratch.Scores[index] = score.value_or(0.0f);
            writer.Finished(score.has_value(), scratch.Scores[index]);
        }
        result = Decide(table, context, scratch, Interrupted(table, context.Entities[0]));
    }
    if (trace != nullptr)
        trace->Result = result;
    return result;
}
