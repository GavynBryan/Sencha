#include <logic/VerbSequenceRunner.h>

#include <authored/AuthoredQueryDispatcher.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>

namespace
{
// Deeper than any authored nesting needs; a cycle cannot form, since a value
// names queries and never another value.
constexpr std::uint32_t kQueryNestingLimit = 8;

Vec3d VectorOf(const AuthoredVectorValue& value)
{
    return Vec3d(static_cast<float>(value.Components[0]), static_cast<float>(value.Components[1]),
                 static_cast<float>(value.Components[2]));
}

AuthoredValue VectorValue(const Vec3d& vector)
{
    AuthoredVectorValue value;
    value.Length = 3;
    value.Components[0] = vector.X;
    value.Components[1] = vector.Y;
    value.Components[2] = vector.Z;
    return AuthoredValue::Vector(value);
}

bool EntityAt(const SequenceRun& run, std::uint8_t port, EntityId& out)
{
    return port < run.Ports.size() && run.Ports[port].TryGetEntity(out) && run.Entities.IsAlive(out);
}

bool TurnIntoWorld(const SequenceRun& run, std::uint8_t frame, AuthoredValue& value)
{
    EntityId entity;
    AuthoredVectorValue vector;
    if (!EntityAt(run, frame, entity) || !value.TryGetVector(vector) || vector.Length != 3
        || !run.Entities.IsRegistered<WorldTransform>())
        return false;
    const WorldTransform* pose = run.Entities.TryGet<WorldTransform>(entity);
    if (pose == nullptr)
        return false;
    value = VectorValue(pose->Value.Rotation.RotateVector(VectorOf(vector)));
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
        if (!Ask(value.Query, value.Arguments, run, depth, answer))
            return false;
        if (value.Member >= 0)
        {
            const std::span<const AuthoredValue> members = answer.Children();
            if (static_cast<std::size_t>(value.Member) >= members.size())
                return false;
            out = members[static_cast<std::size_t>(value.Member)];
        }
        else
        {
            out = std::move(answer);
        }
        break;
    }
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
        bool truth = false;
        return Ask(condition.Query, condition.Arguments, run, depth, answer) && answer.TryGetBool(truth)
            && truth == condition.Expect;
    }

    EntityId entity;
    if (!EntityAt(run, condition.Port, entity))
        return false;
    if (std::ranges::any_of(condition.Components,
                            [&](ComponentId component) { return !run.Entities.HasComponent(entity, component); }))
        return false;
    if (!condition.HasTags)
        return true;
    static const GameplayTagContainer kNoTags{};
    const GameplayTagContainer* tags = run.Entities.IsRegistered<GameplayTagContainer>()
        ? run.Entities.TryGet<GameplayTagContainer>(entity)
        : nullptr;
    return run.Tags != nullptr && condition.Tags.Matches(tags != nullptr ? *tags : kNoTags, *run.Tags);
}

bool VerbSequenceRunner::Holds(const CompiledVerbSequence& sequence, const SequenceRun& run)
{
    return std::ranges::all_of(sequence.Conditions, [&](const CompiledSequenceCondition& condition) {
        return HoldsCondition(condition, run, 0);
    });
}

SequenceOutcome VerbSequenceRunner::Run(const CompiledVerbSequence& sequence, const SequenceRun& run)
{
    SequenceOutcome outcome;
    if (!Holds(sequence, run))
        return outcome;
    outcome.Held = true;

    for (const CompiledSequenceCommand& command : sequence.Commands)
    {
        VerbAdmission status = VerbAdmission::UnresolvedBinding;
        if (command.Resolved)
        {
            Inputs.resize(command.Inputs.size());
            bool read = true;
            for (std::size_t i = 0; i < command.Inputs.size() && read; ++i)
                read = Read(command.Inputs[i], run, 0, Inputs[i]);
            status = read ? run.Verbs.Invoke(command.Binding, Inputs, run.Source).Status
                          : VerbAdmission::InvalidArguments;
        }
        if (status == VerbAdmission::Accepted)
        {
            ++outcome.Invoked;
            continue;
        }
        if (outcome.Failed++ == 0)
            outcome.FirstFailure = status;
        if (command.OnFailure == SequenceFailurePolicy::Stop)
        {
            outcome.Stopped = true;
            break;
        }
    }
    return outcome;
}
