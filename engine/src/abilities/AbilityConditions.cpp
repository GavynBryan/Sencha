#include "AbilityPass.h"

#include <attributes/AttributeSet.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <controller/Aim.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    bool Compare(double left, AbilityCompare compare, double right)
    {
        switch (compare)
        {
        case AbilityCompare::AtLeast: return left >= right;
        case AbilityCompare::AtMost: return left <= right;
        case AbilityCompare::Above: return left > right;
        case AbilityCompare::Below: return left < right;
        case AbilityCompare::Equals: return left == right;
        case AbilityCompare::NotEquals: return left != right;
        }
        return false;
    }

    AuthoredValue NumberAs(double number, DataFieldKind kind)
    {
        switch (kind)
        {
        case DataFieldKind::Int: return AuthoredValue::Int(std::llround(number));
        case DataFieldKind::Bool: return AuthoredValue::Bool(number != 0.0);
        default: return AuthoredValue::Float(number);
        }
    }

    AuthoredValue VectorOf(const Vec3d& vector)
    {
        AuthoredVectorValue value;
        value.Length = 3;
        value.Components[0] = vector.X;
        value.Components[1] = vector.Y;
        value.Components[2] = vector.Z;
        return AuthoredValue::Vector(value);
    }
}

std::uint64_t AbilityPass::ClockOf(const AbilityActor& actor, AbilityTimeDomain domain) const
{
    const World& world = Entities;
    if (domain == AbilityTimeDomain::Actor && world.IsRegistered<ActorTime>())
        if (const ActorTime* time = world.TryGet<ActorTime>(actor.Entity))
            return ActorTicks(*time);
    return Now.TickIndex;
}

std::uint64_t AbilityPass::PhaseTicks(const AbilityActor& actor, const AbilityRecord& record,
                                      const AbilityDefinition& definition) const
{
    const std::uint64_t now = ClockOf(actor, definition.Phases[record.Phase].Domain);
    return now > record.PhaseStart ? now - record.PhaseStart : 0;
}

EntityId AbilityPass::EntityOf(const AbilityActor& actor, std::size_t slot, const AbilityValue& value) const
{
    if (value.Source == AbilityValueSource::Port)
    {
        switch (value.Port)
        {
        case AbilityPort::Actor: return actor.Entity;
        case AbilityPort::Instigator:
            return slot < kAbilityRecordCapacity ? actor.Record(slot).Instigator : actor.Entity;
        case AbilityPort::Target: return Target;
        default: return EntityId{};
        }
    }
    if (value.Source == AbilityValueSource::Local && slot < kAbilityRecordCapacity && value.Local != kAbilityNoLocal)
    {
        const AbilityLocalValue& local = actor.Locals(slot).Values[value.Local];
        if (local.Set && (value.Field != AbilityHitField::Entity || local.Hit))
            return local.Entity;
    }
    EntityId constant;
    if (value.Source == AbilityValueSource::Constant && value.Constant.TryGetEntity(constant))
        return constant;
    return EntityId{};
}

double AbilityPass::Number(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                           const AbilityValue& value)
{
    switch (value.Source)
    {
    case AbilityValueSource::Constant:
    {
        double number = 0.0;
        std::int64_t whole = 0;
        bool truth = false;
        if (value.Constant.TryGetFloat(number))
            return number;
        if (value.Constant.TryGetInt(whole))
            return static_cast<double>(whole);
        return value.Constant.TryGetBool(truth) && truth ? 1.0 : 0.0;
    }
    case AbilityValueSource::Local:
        return slot < kAbilityRecordCapacity && value.Local != kAbilityNoLocal
            ? actor.Locals(slot).Values[value.Local].Number
            : 0.0;
    case AbilityValueSource::Port:
    {
        if (slot >= kAbilityRecordCapacity)
            return 0.0;
        const AbilityRecord& record = actor.Record(slot);
        switch (value.Port)
        {
        case AbilityPort::PhaseTicks: return static_cast<double>(PhaseTicks(actor, record, definition));
        case AbilityPort::Pulse: return static_cast<double>(record.Pulse);
        case AbilityPort::PhaseProgress:
        {
            const std::uint32_t full = definition.Phases[record.Phase].ProgressTicks;
            return full == 0 ? 0.0
                             : std::min(1.0, static_cast<double>(PhaseTicks(actor, record, definition)) / full);
        }
        case AbilityPort::TimeScale:
        {
            const World& world = Entities;
            const ActorTime* time = world.IsRegistered<ActorTime>() ? world.TryGet<ActorTime>(actor.Entity) : nullptr;
            return time != nullptr ? ActorScaleValue(time->ScaleQ16) : 1.0;
        }
        default: return 0.0;
        }
    }
    }
    return 0.0;
}

void AbilityPass::Evaluate(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                           const AbilityValue& value, AuthoredValue& out)
{
    if (value.Source == AbilityValueSource::Constant)
    {
        out = value.Constant;
        return;
    }
    if (value.Source == AbilityValueSource::Port)
    {
        switch (value.Port)
        {
        case AbilityPort::Actor:
        case AbilityPort::Instigator:
        case AbilityPort::Target: out = AuthoredValue::Entity(EntityOf(actor, slot, value)); return;
        case AbilityPort::AimOrigin: out = VectorOf(AimOf(Entities, actor.Entity).Origin); return;
        case AbilityPort::AimDirection: out = VectorOf(AimOf(Entities, actor.Entity).Direction); return;
        default: out = NumberAs(Number(actor, slot, definition, value), value.As); return;
        }
    }

    if (slot >= kAbilityRecordCapacity || value.Local == kAbilityNoLocal)
    {
        out = AuthoredValue{};
        return;
    }
    const AbilityLocalValue& local = actor.Locals(slot).Values[value.Local];
    switch (definition.Locals[value.Local].Type)
    {
    case AbilityLocalType::Bool: out = AuthoredValue::Bool(local.Number != 0.0); return;
    case AbilityLocalType::Int:
    case AbilityLocalType::Float: out = NumberAs(local.Number, value.As); return;
    case AbilityLocalType::Vec3: out = VectorOf(local.Point); return;
    case AbilityLocalType::Entity: out = AuthoredValue::Entity(local.Entity); return;
    case AbilityLocalType::Hit:
        out = value.Field == AbilityHitField::Entity ? AuthoredValue::Entity(local.Entity)
            : value.Field == AbilityHitField::Normal ? VectorOf(local.Normal)
                                                     : VectorOf(local.Point);
        return;
    case AbilityLocalType::Entities: out = AuthoredValue{}; return;
    }
}

bool AbilityPass::AllHold(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                          const std::vector<std::uint16_t>& conditions, std::uint16_t& failing)
{
    for (const std::uint16_t condition : conditions)
    {
        if (!Holds(actor, slot, definition, condition))
        {
            failing = condition;
            return false;
        }
    }
    return true;
}

bool AbilityPass::Holds(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                        std::uint16_t index)
{
    const AbilityCondition& condition = definition.Conditions[index];
    const World& world = Entities;
    switch (condition.Kind)
    {
    case AbilityConditionKind::Tags:
    {
        static const GameplayTagContainer kNoTags{};
        const GameplayTagContainer* tags = actor.Tags();
        return TagRegistry != nullptr && condition.Tags.Matches(tags != nullptr ? *tags : kNoTags, *TagRegistry);
    }
    case AbilityConditionKind::Attribute:
    {
        const AttributeSet* set = world.IsRegistered<AttributeSet>() ? world.TryGet<AttributeSet>(actor.Entity) : nullptr;
        if (set == nullptr || !set->Has(condition.Attribute))
            return false;
        return Compare(set->GetCurrent(condition.Attribute), condition.Compare,
                       Number(actor, slot, definition, condition.Value));
    }
    case AbilityConditionKind::Local:
        return slot < kAbilityRecordCapacity && actor.Locals(slot).Values[condition.Subject.Local].Set
            && Compare(actor.Locals(slot).Values[condition.Subject.Local].Number, condition.Compare,
                       Number(actor, slot, definition, condition.Value));
    case AbilityConditionKind::Hit:
    {
        if (slot >= kAbilityRecordCapacity)
            return false;
        const AbilityLocalValue& hit = actor.Locals(slot).Values[condition.Subject.Local];
        return hit.Set && hit.Hit;
    }
    case AbilityConditionKind::Entity:
    {
        const EntityId entity = EntityOf(actor, slot, condition.Subject);
        if (!world.IsAlive(entity))
            return false;
        for (const ComponentId component : condition.Components)
            if (!world.HasComponent(entity, component))
                return false;
        if (!condition.HasTags)
            return true;
        static const GameplayTagContainer kNoTags{};
        const GameplayTagContainer* tags =
            world.IsRegistered<GameplayTagContainer>() ? world.TryGet<GameplayTagContainer>(entity) : nullptr;
        return TagRegistry != nullptr && condition.Tags.Matches(tags != nullptr ? *tags : kNoTags, *TagRegistry);
    }
    case AbilityConditionKind::PhaseTicks:
        return slot < kAbilityRecordCapacity
            && Compare(static_cast<double>(PhaseTicks(actor, actor.Record(slot), definition)), condition.Compare,
                       Number(actor, slot, definition, condition.Value));
    case AbilityConditionKind::Pulse:
        return slot < kAbilityRecordCapacity
            && Compare(static_cast<double>(actor.Record(slot).Pulse), condition.Compare,
                       Number(actor, slot, definition, condition.Value));
    case AbilityConditionKind::Input:
    {
        const std::uint8_t action = slot < kAbilityRecordCapacity ? actor.Record(slot).TriggerAction : kAbilityNoTrigger;
        const bool held = action != kAbilityNoTrigger && action < actor.Input.Values.size()
            && actor.Input.Values[action].IsHeld();
        return held == condition.Held;
    }
    case AbilityConditionKind::Query:
    {
        if (Host.Queries == nullptr)
            return false;
        Inputs.resize(condition.Arguments.size());
        for (std::size_t i = 0; i < condition.Arguments.size(); ++i)
            Evaluate(actor, slot, definition, condition.Arguments[i], Inputs[i]);
        bool truth = false;
        return Host.Queries->Evaluate(condition.Query, Inputs, Answer) == AuthoredQueryStatus::Value
            && Answer.TryGetBool(truth) && truth;
    }
    case AbilityConditionKind::All:
        return std::ranges::all_of(condition.Children,
                                   [&](std::uint16_t child) { return Holds(actor, slot, definition, child); });
    case AbilityConditionKind::Any:
        return std::ranges::any_of(condition.Children,
                                   [&](std::uint16_t child) { return Holds(actor, slot, definition, child); });
    case AbilityConditionKind::Not:
        return !condition.Children.empty() && !Holds(actor, slot, definition, condition.Children.front());
    }
    return false;
}
