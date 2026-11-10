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
    SelectionResult result;
    if (trace != nullptr)
    {
        trace->Options.clear();
        trace->Values.clear();
    }
    if (!ContextFits(table, context))
    {
        if (trace != nullptr)
            trace->Result = result;
        return result;
    }

    const std::span<const SelectionOption> options = table.Options();
    scratch.Scores.assign(options.size(), 0.0f);
    scratch.Qualified.assign(options.size(), 0);
    for (std::uint32_t optionIndex = 0; optionIndex < options.size(); ++optionIndex)
    {
        const std::span<const SelectionConsideration> considerations = options[optionIndex].Considerations;
        SelectionTraceOption* traced = nullptr;
        if (trace != nullptr)
        {
            traced = &trace->Options.emplace_back();
            traced->FirstValue = static_cast<std::uint32_t>(trace->Values.size());
            trace->Values.resize(trace->Values.size() + considerations.size());
        }
        const auto record = [&](std::uint32_t index, float value, float output, SelectionValueStatus status) {
            if (traced != nullptr)
                trace->Values[traced->FirstValue + index] = { value, output, status };
        };

        const bool unavailable = optionIndex < 64 && (context.Unavailable >> optionIndex & 1u) != 0;
        if (traced != nullptr)
            traced->Unavailable = unavailable;
        bool qualified = !unavailable;
        for (std::uint32_t index = 0; qualified && index < considerations.size(); ++index)
        {
            const SelectionConsideration& consideration = considerations[index];
            const ConsiderationRule& rule = consideration.Rule;
            if (rule.Mode != ConsiderationMode::Require)
                continue;
            const std::optional<float> value = Measure(consideration, context, scratch);
            const bool passes = value ? rule.Admits(*value) : rule.UnmeasuredScore.has_value();
            record(index, value.value_or(0.0f), passes ? 1.0f : 0.0f,
                   value ? SelectionValueStatus::Measured : SelectionValueStatus::Unmeasured);
            if (!passes)
            {
                qualified = false;
                if (traced != nullptr)
                    traced->RejectedBy = index;
            }
        }

        float sum = 0.0f;
        float weights = 0.0f;
        for (std::uint32_t index = 0; qualified && index < considerations.size(); ++index)
        {
            const SelectionConsideration& consideration = considerations[index];
            const ConsiderationRule& rule = consideration.Rule;
            if (rule.Mode != ConsiderationMode::Score)
                continue;
            const std::optional<float> value = Measure(consideration, context, scratch);
            if (!value && !rule.UnmeasuredScore)
            {
                record(index, 0.0f, 0.0f, SelectionValueStatus::Unmeasured);
                qualified = false;
                if (traced != nullptr)
                    traced->RejectedBy = index;
                break;
            }
            const float output = value ? rule.Output(*value) : *rule.UnmeasuredScore;
            record(index, value.value_or(0.0f), output,
                   value ? SelectionValueStatus::Measured : SelectionValueStatus::Unmeasured);
            sum += rule.Weight * output;
            weights += rule.Weight;
        }

        scratch.Qualified[optionIndex] = qualified ? 1 : 0;
        scratch.Scores[optionIndex] = qualified ? (weights > 0.0f ? sum / weights : 1.0f) : 0.0f;
        if (traced != nullptr)
        {
            traced->Qualified = qualified;
            traced->Score = scratch.Scores[optionIndex];
        }
    }

    result.Interrupted = Interrupted(table, context.Entities[0]);
    const bool currentQualifies = context.Current && scratch.Qualified[*context.Current] != 0;
    std::optional<std::uint32_t> best;
    for (std::uint32_t index = 0; index < options.size(); ++index)
        if (scratch.Qualified[index] != 0 && (!best || scratch.Scores[index] > scratch.Scores[*best]))
            best = index;

    const auto finish = [&](std::uint32_t option, SelectionDecision decision) {
        result.Status = SelectionStatus::Success;
        result.Option = option;
        result.Score = scratch.Scores[option];
        result.Decision = decision;
    };
    if (currentQualifies && !result.Interrupted
        && context.CurrentSeconds < options[*context.Current].CommitSeconds)
        finish(*context.Current, SelectionDecision::Committed);
    else if (!best)
        result.Status = SelectionStatus::NoneQualified;
    else if (currentQualifies && *best != *context.Current
             && scratch.Scores[*best] < scratch.Scores[*context.Current] + table.SwitchMargin())
        finish(*context.Current, SelectionDecision::WithinMargin);
    else if (table.Mode() == SelectionMode::PickFromBand)
    {
        const float floor = scratch.Scores[*best] - table.BandWidth();
        SeededBandPick band(context.Seed);
        std::uint32_t pick = *best;
        for (std::uint32_t index = 0; index < options.size(); ++index)
        {
            if (scratch.Qualified[index] != 0 && scratch.Scores[index] >= floor && band.Offer(index))
                pick = index;
        }
        finish(pick, SelectionDecision::Chosen);
    }
    else
        finish(*best, SelectionDecision::Chosen);

    if (trace != nullptr)
        trace->Result = result;
    return result;
}
