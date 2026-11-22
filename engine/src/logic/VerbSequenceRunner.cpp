#include <logic/VerbSequenceRunner.h>

#include <authored/AuthoredQueryDispatcher.h>
#include <authored/AuthoredValueTraits.h>
#include <ecs/World.h>
#include <gameplay_tags/EntityMatch.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <utility>

namespace
{
// Deeper than any authored nesting needs; a cycle cannot form, since a value
// names queries and never another value.
constexpr std::uint32_t kQueryNestingLimit = 8;

bool EntityAt(const SequenceRun& run, std::uint8_t port, EntityId& out)
{
    return port < run.Ports.size() && run.Ports[port].TryGetEntity(out) && run.Entities.IsAlive(out);
}

bool TurnIntoWorld(const SequenceRun& run, std::uint8_t frame, AuthoredValue& value)
{
    EntityId entity;
    Vec3d vector;
    if (!EntityAt(run, frame, entity) || !AuthoredValueTraits<Vec3d>::Decode(value, vector)
        || !run.Entities.IsRegistered<WorldTransform>())
        return false;
    const WorldTransform* pose = run.Entities.TryGet<WorldTransform>(entity);
    if (pose == nullptr)
        return false;
    value = AuthoredValueTraits<Vec3d>::Encode(pose->Value.Rotation.RotateVector(vector));
    return true;
}

template<typename Answer>
bool MemberOf(const CompiledSequenceValue& value, Answer&& answer, AuthoredValue& out)
{
    if (value.Member < 0)
    {
        out = std::forward<Answer>(answer);
        return true;
    }
    const std::span<const AuthoredValue> members = answer.Children();
    if (static_cast<std::size_t>(value.Member) >= members.size())
        return false;
    out = members[static_cast<std::size_t>(value.Member)];
    return true;
}

bool ScaleBy(float scale, AuthoredValue& value)
{
    double number = 0.0;
    AuthoredVectorValue vector;
    if (value.TryGetFloat(number))
    {
        value = AuthoredValue::Float(number * scale);
        return true;
    }
    if (value.TryGetVector(vector))
    {
        for (std::uint8_t i = 0; i < vector.Length; ++i)
            vector.Components[i] *= scale;
        value = AuthoredValue::Vector(vector);
        return true;
    }
    return false;
}
} // namespace

bool VerbSequenceRunner::Ask(const AuthoredQueryHandle& query, std::span<const CompiledSequenceValue> arguments,
                             const SequenceRun& run, std::uint32_t depth, AuthoredValue& answer)
{
    if (run.Queries == nullptr || depth >= kQueryNestingLimit)
        return false;
    // Sized once, so a nested Ask never moves the pack an outer one is filling.
    if (QueryArguments.size() < kQueryNestingLimit)
        QueryArguments.resize(kQueryNestingLimit);
    QueryArguments[depth].resize(arguments.size());
    for (std::size_t i = 0; i < arguments.size(); ++i)
        if (!Read(arguments[i], run, depth + 1, QueryArguments[depth][i]))
            return false;
    return run.Queries->Evaluate(query, QueryArguments[depth], answer) == AuthoredQueryStatus::Value;
}

bool VerbSequenceRunner::Read(const CompiledSequenceValue& value, const SequenceRun& run, std::uint32_t depth,
                              AuthoredValue& out)
{
    switch (value.Source)
    {
    case SequenceValueSource::Constant:
        out = value.Constant;
        break;
    case SequenceValueSource::Port:
        if (value.Port >= run.Ports.size())
            return false;
        out = run.Ports[value.Port];
        break;
    case SequenceValueSource::Query:
    {
        AuthoredValue answer;
        if (!Ask(value.Query, value.Arguments, run, depth, answer) || !MemberOf(value, std::move(answer), out))
            return false;
        break;
    }
    case SequenceValueSource::Result:
        if (value.Result >= Results.size() || Results[value.Result].IsNone()
            || !MemberOf(value, Results[value.Result], out))
            return false;
        break;
    }
    if (value.Frame != kNoSequencePort && !TurnIntoWorld(run, value.Frame, out))
        return false;
    return !value.Scale || ScaleBy(*value.Scale, out);
}

bool VerbSequenceRunner::HoldsCondition(const CompiledSequenceCondition& condition, const SequenceRun& run,
                                        std::uint32_t depth)
{
    if (!condition.Resolved)
        return false;
    if (condition.Kind == SequenceConditionKind::Query)
    {
        AuthoredValue answer;
        double number = 0.0;
        return Ask(condition.Query, condition.Arguments, run, depth, answer) && answer.TryGetNumber(number)
            && condition.Require.Admits(number);
    }

    if (condition.Kind == SequenceConditionKind::Tag)
    {
        GameplayTagId tag;
        return condition.Port < run.Ports.size() && run.Ports[condition.Port].TryGetTag(tag) && run.Tags != nullptr
            && condition.Tags.Matches(tag, *run.Tags);
    }

    EntityId entity;
    return condition.Port < run.Ports.size() && run.Ports[condition.Port].TryGetEntity(entity)
        && EntityMatches(run.Entities, entity, condition.Components, condition.HasTags ? &condition.Tags : nullptr,
                         run.Tags);
}

bool VerbSequenceRunner::Holds(const CompiledVerbSequence& sequence, const SequenceRun& run)
{
    return std::ranges::all_of(sequence.Conditions, [&](const CompiledSequenceCondition& condition) {
        return HoldsCondition(condition, run, 0);
    });
}

VerbAnswer VerbSequenceRunner::Invoke(const CompiledSequenceCommand& command, const SequenceRun& run)
{
    Inputs.resize(command.Inputs.size());
    for (std::size_t i = 0; i < command.Inputs.size(); ++i)
        if (!Read(command.Inputs[i], run, 0, Inputs[i]))
            return VerbArgumentAnswer(VerbAdmission::InvalidArguments,
                                      command.Binding.Inputs[i].Destinations.front().ArgumentSlot);
    AuthoredValue* answered = command.Result < Results.size() ? &Results[command.Result] : nullptr;
    const VerbInvocationResult result = run.Verbs.Invoke(command.Binding, Inputs, run.Source, answered);
    return VerbAnswer(result.Status, result.Detail);
}

SequenceOutcome VerbSequenceRunner::Run(const CompiledVerbSequence& sequence, const SequenceRun& run)
{
    SequenceOutcome outcome;
    if (!Holds(sequence, run))
        return outcome;
    outcome.Held = true;
    Results.resize(sequence.Results);
    for (AuthoredValue& result : Results)
        result = AuthoredValue{};

    for (std::size_t index = 0; index < sequence.Commands.size(); ++index)
    {
        const CompiledSequenceCommand& command = sequence.Commands[index];
        const VerbAnswer answer = command.Resolved ? Invoke(command, run) : VerbAnswer(VerbAdmission::UnresolvedBinding);
        if (answer.Accepted())
        {
            ++outcome.Invoked;
            continue;
        }
        if (outcome.Failed++ == 0)
        {
            outcome.FirstFailure = answer.Status;
            outcome.FirstFailureDetail = answer.Detail;
            outcome.FirstFailedCommand = static_cast<std::uint32_t>(index);
        }
        if (command.OnFailure == SequenceFailurePolicy::Stop)
        {
            outcome.Stopped = true;
            break;
        }
    }
    return outcome;
}
