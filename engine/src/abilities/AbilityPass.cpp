#include "AbilityPass.h"

#include <abilities/AbilityCueLog.h>
#include <abilities/AbilityRegistry.h>
#include <attributes/AttributeRegistry.h>
#include <authored/AuthoredQueryDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>

AbilityPass::AbilityPass(World& world, AbilityPassBuffers& buffers, const AbilityHost& host, const FixedSimTime& now,
                         std::uint32_t stepBudget, std::uint32_t lingerTicks)
    : Entities(world)
    , Host(host)
    , Now(now)
    , StepBudget(std::max<std::uint32_t>(stepBudget, 1))
    , LingerTicks(lingerTicks)
    , Abilities(world.TryGetResource<AbilityRegistry>())
    , TagRegistry(world.TryGetResource<GameplayTagRegistry>())
    , Attributes(world.TryGetResource<AttributeRegistry>())
    , History(world.TryGetResource<AbilityTrace>())
    , Inputs(buffers.Inputs)
    , Answer(buffers.Answer)
    , Costs(buffers.Costs)
{
}

const AbilityDefinition* AbilityPass::DefinitionOf(const AbilityRecord& record) const
{
    return Abilities != nullptr ? Abilities->Get(record.Key.Ability) : nullptr;
}

void AbilityPass::Trace(AbilityTraceRecord record)
{
    if (History == nullptr)
        return;
    record.Tick = Now.TickIndex;
    History->Record(record);
}

bool AbilityPass::Live(const AbilityActor& actor, std::size_t slot) const
{
    return actor.Alive() && actor.Record(slot).State == AbilityRecordState::Live;
}

LeaseOwner AbilityPass::OwnerOf(const AbilityActor& actor, std::size_t slot, std::uint8_t op) const
{
    return LeaseOwner{ .Entity = actor.Entity, .Key = (actor.Record(slot).Sequence << 8) | op };
}

int AbilityPass::ClaimSlot(const AbilityActor& actor)
{
    ActiveAbilities& active = actor.Active();
    std::size_t live = 0;
    int free = -1;
    int oldest = -1;
    for (std::size_t i = 0; i < kAbilityRecordCapacity; ++i)
    {
        const AbilityRecord& record = active.Records[i];
        if (record.State == AbilityRecordState::Live)
            ++live;
        else if (record.State == AbilityRecordState::Free && free < 0)
            free = static_cast<int>(i);
        else if (record.State == AbilityRecordState::Lingering
                 && (oldest < 0 || record.Sequence < active.Records[oldest].Sequence))
            oldest = static_cast<int>(i);
    }
    if (live >= kAbilityLiveRecordCapacity)
        return -1;
    if (free >= 0)
        return free;
    ++active.Evicted;
    return oldest;
}

AbilityRefusal AbilityPass::CanActivate(const AbilityActor& actor, const AbilityGrant& grant, std::uint16_t& failing)
{
    failing = kAbilityNoCondition;
    const AbilityDefinition* definition = Abilities != nullptr ? Abilities->Get(grant.Ability) : nullptr;
    if (definition == nullptr || definition->Phases.empty())
        return AbilityRefusal::Undefined;
    if (!AllHold(actor, kAbilityRecordCapacity, *definition, definition->Activation, failing))
        return AbilityRefusal::ActivationFailed;

    std::size_t live = 0;
    std::size_t instances = 0;
    for (const AbilityRecord& record : actor.Active().Records)
    {
        if (record.State != AbilityRecordState::Live)
            continue;
        ++live;
        instances += record.Key.Ability == grant.Ability ? 1 : 0;
    }
    if (instances >= definition->Instances)
        return AbilityRefusal::Instances;
    if (live >= kAbilityLiveRecordCapacity)
        return AbilityRefusal::Capacity;

    const bool cooldown = definition->Cooldown && definition->Cooldown->Start == AbilityCooldownStart::OnStart;
    if ((definition->StartGroups != 0 || cooldown) && !CheckCosts(actor, *definition, definition->StartGroups, cooldown).Paid)
        return AbilityRefusal::CostFailed;
    return AbilityRefusal::None;
}

int AbilityPass::TryStart(const AbilityActor& actor, const AbilityGrant& grant, AbilityActivationKey key,
                          std::uint8_t triggerAction, EntityId instigator)
{
    std::uint16_t failing = kAbilityNoCondition;
    const AbilityRefusal refusal = CanActivate(actor, grant, failing);
    if (refusal != AbilityRefusal::None)
    {
        Trace({ .Actor = actor.Entity, .Key = key, .Event = AbilityTraceEvent::Refused, .Refusal = refusal,
                .Element = failing });
        return -1;
    }
    const int claimed = ClaimSlot(actor);
    const auto slot = static_cast<std::size_t>(claimed);
    const AbilityDefinition& definition = *Abilities->Get(grant.Ability);

    ActiveAbilities& active = actor.Active();
    AbilityRecord& record = active.Records[slot];
    record = AbilityRecord{};
    record.Key = key;
    record.Grant = grant.Id;
    record.State = AbilityRecordState::Live;
    record.Revision = Abilities->Revision(grant.Ability);
    record.TriggerAction = triggerAction;
    record.TriggerSource = actor.Source;
    record.Instigator = instigator.IsValid() ? instigator : actor.Entity;
    record.Sequence = active.NextSequence++;
    actor.Locals(slot) = AbilityRecordLocals{};

    (void)Commit(actor, slot, definition, definition.StartGroups);
    if (definition.Cooldown && definition.Cooldown->Start == AbilityCooldownStart::OnStart)
        StartCooldown(actor, slot, definition);

    if (GameplayTagContainer* tags = actor.Tags())
    {
        AbilityRecord& granted = actor.Record(slot);
        for (GameplayTagId tag : definition.OwnedTags)
        {
            if (tags->StackCount(tag) == 0 && tags->Size() >= GameplayTagContainer::Capacity)
                continue;
            tags->Grant(tag);
            granted.OwnedTags[granted.OwnedTagCount++] = tag;
        }
    }
    Trace({ .Actor = actor.Entity, .Key = key, .Event = AbilityTraceEvent::Started });

    for (std::size_t other = 0; other < kAbilityRecordCapacity && !definition.CancelTags.empty(); ++other)
    {
        if (other == slot || !Live(actor, other))
            continue;
        const AbilityDefinition* running = DefinitionOf(actor.Record(other));
        if (running == nullptr || TagRegistry == nullptr)
            continue;
        const bool cancelled = std::ranges::any_of(running->OwnedTags, [&](GameplayTagId owned) {
            return std::ranges::any_of(definition.CancelTags, [&](GameplayTagId cancel) {
                return owned == cancel || TagRegistry->IsDescendantOf(owned, cancel);
            });
        });
        if (cancelled)
            End(actor, other, AbilityEndReason::CancelledByAbility);
    }

    EnterPhase(actor, slot, definition, 0);
    Run(actor, slot);
    return claimed;
}

void AbilityPass::EnterPhase(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                             std::uint8_t phase)
{
    const AbilityPhase& entered = definition.Phases[phase];
    AbilityRecord& record = actor.Record(slot);
    record.Phase = phase;
    record.PhaseStart = ClockOf(actor, entered.Domain);
    record.Pulse = 0;
    record.NextPulse = entered.PulseFirstOnEntry ? 0 : entered.PulseEvery;

    if (!RunOps(actor, slot, definition, entered.Hold) || !RunOps(actor, slot, definition, entered.Enter))
        return;
    if (entered.Pulses && entered.PulseFirstOnEntry)
        Pulse(actor, slot, definition);
}

void AbilityPass::LeavePhase(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                             bool runExit)
{
    if (runExit && !RunOps(actor, slot, definition, definition.Phases[actor.Record(slot).Phase].Exit))
        return;
    ReleaseLeases(actor, slot);
}

void AbilityPass::ReleaseLeases(const AbilityActor& actor, std::size_t slot)
{
    // Newest first, so a hold taken on top of another is let go before it.
    AbilityRecord& record = actor.Record(slot);
    const VerbInvocationSource source{ .Producer = actor.Entity, .Instigator = record.Instigator, .Tick = Now.TickIndex };
    while (record.LeaseCount > 0)
    {
        const AbilityHeldLease held = record.Leases[--record.LeaseCount];
        if (Host.Verbs != nullptr)
            (void)Host.Verbs->ReleaseLease(held.Lease, OwnerOf(actor, slot, held.Op), source);
    }
}

void AbilityPass::Pulse(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition)
{
    const AbilityPhase& phase = definition.Phases[actor.Record(slot).Phase];
    if (!RunOps(actor, slot, definition, phase.PulseOps))
        return;
    AbilityRecord& record = actor.Record(slot);
    ++record.Pulse;
    record.NextPulse += phase.PulseEvery;
}

bool AbilityPass::HoldsCompleted(const AbilityActor& actor, std::size_t slot)
{
    if (Host.Verbs == nullptr)
        return false;
    const VerbRegistry& verbs = Host.Verbs->Registry();
    const AbilityRecord& record = actor.Record(slot);
    bool completing = false;
    for (std::size_t i = 0; i < record.LeaseCount; ++i)
    {
        const AbilityHeldLease& held = record.Leases[i];
        if (!verbs.IsLive(held.Lease) || !verbs.Get(held.Lease)->Completes)
            continue;
        completing = true;
        if (Host.Verbs->LeaseHolds(held.Lease, OwnerOf(actor, slot, held.Op)))
            return false;
    }
    return completing;
}

void AbilityPass::Run(const AbilityActor& actor, std::size_t slot)
{
    std::uint32_t steps = 0;
    while (Live(actor, slot))
    {
        const AbilityDefinition* definition = DefinitionOf(actor.Record(slot));
        if (definition == nullptr)
            return;
        const AbilityPhase& phase = definition->Phases[actor.Record(slot).Phase];
        const std::uint64_t ticks = PhaseTicks(actor, actor.Record(slot), *definition);

        const AbilityTransition* chosen = nullptr;
        std::uint16_t chosenIndex = 0;
        for (std::size_t t = 0; t < phase.Then.size() && chosen == nullptr; ++t)
        {
            const AbilityTransition& transition = phase.Then[t];
            const bool triggered = transition.Trigger == AbilityTransitionTrigger::Immediate
                || (transition.Trigger == AbilityTransitionTrigger::Release && actor.Record(slot).Released)
                || (transition.Trigger == AbilityTransitionTrigger::Timeout && ticks >= transition.Ticks)
                || (transition.Trigger == AbilityTransitionTrigger::Complete && HoldsCompleted(actor, slot));
            if (!triggered)
                continue;
            if (transition.When != kAbilityNoCondition && !Holds(actor, slot, *definition, transition.When))
                continue;
            chosen = &transition;
            chosenIndex = static_cast<std::uint16_t>(t);
        }
        if (chosen == nullptr)
            return;
        if (++steps > StepBudget)
        {
            End(actor, slot, AbilityEndReason::StepBudget, chosenIndex);
            return;
        }
        Trace({ .Actor = actor.Entity, .Key = actor.Record(slot).Key, .Event = AbilityTraceEvent::Transition,
                .Phase = chosen->To, .Element = chosenIndex });
        const std::uint8_t to = chosen->To;
        LeavePhase(actor, slot, *definition, true);
        if (!Live(actor, slot))
            return;
        if (to == kAbilityEndPhase)
        {
            End(actor, slot, AbilityEndReason::Completed);
            return;
        }
        EnterPhase(actor, slot, *definition, to);
    }
}

void AbilityPass::End(const AbilityActor& actor, std::size_t slot, AbilityEndReason reason, std::uint16_t element)
{
    if (!Live(actor, slot))
        return;
    const AbilityDefinition* definition = DefinitionOf(actor.Record(slot));
    // Exit ops never run on an End; the leases still go.
    ReleaseLeases(actor, slot);
    if (GameplayTagContainer* tags = actor.Tags())
    {
        const AbilityRecord& record = actor.Record(slot);
        for (std::uint8_t i = 0; i < record.OwnedTagCount; ++i)
            tags->Revoke(record.OwnedTags[i]);
    }
    if (definition != nullptr && definition->Cooldown && definition->Cooldown->Start == AbilityCooldownStart::OnEnd
        && !actor.Record(slot).CooldownStarted)
        StartCooldown(actor, slot, *definition);

    AbilityRecord& record = actor.Record(slot);
    record.OwnedTagCount = 0;
    record.State = AbilityRecordState::Lingering;
    record.EndReason = reason;
    record.EndElement = element;
    record.EndTick = Now.TickIndex;
    Trace({ .Actor = actor.Entity, .Key = record.Key, .Event = AbilityTraceEvent::Ended, .EndReason = reason,
            .Phase = record.Phase, .Element = element });
}

AbilityPass::CuePlace AbilityPass::PlaceOf(const AuthoredValue& at)
{
    const auto vector = [](const AuthoredValue& value) {
        AuthoredVectorValue read;
        return value.TryGetVector(read) ? Vec3d(static_cast<float>(read.Components[0]),
                                                static_cast<float>(read.Components[1]),
                                                static_cast<float>(read.Components[2]))
                                        : Vec3d::Zero();
    };
    CuePlace place;
    // A hit is {hit, entity, point, normal}; an entity or a point stands alone.
    if (at.Kind() == AuthoredValueKind::Record && at.Children().size() == 4)
    {
        (void)at.Children()[1].TryGetEntity(place.Target);
        place.Point = vector(at.Children()[2]);
        place.Normal = vector(at.Children()[3]);
    }
    else if (!at.TryGetEntity(place.Target))
        place.Point = vector(at);
    return place;
}

bool AbilityPass::RunOps(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                         const std::vector<std::uint16_t>& ops)
{
    for (const std::uint16_t op : ops)
        if (!RunOp(actor, slot, definition, op) || !Live(actor, slot))
            return false;
    return Live(actor, slot);
}

bool AbilityPass::RunOp(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                        std::uint16_t index)
{
    const AbilityOp& op = definition.Ops[index];
    const AbilityActivationKey key = actor.Record(slot).Key;
    if (op.Targets == kAbilityNoLocal)
    {
        if (op.When != kAbilityNoCondition && !Holds(actor, slot, definition, op.When))
        {
            Trace({ .Actor = actor.Entity, .Key = key, .Event = AbilityTraceEvent::Skipped, .Element = index });
            return true;
        }
        return Operate(actor, slot, definition, index);
    }

    // Copied: an element's operation may write the list it came from.
    const AbilityRecordLocals& locals = actor.Locals(slot);
    EntityId targets[kAbilityEntityListCapacity];
    const std::uint8_t count = locals.Values[op.Targets].Set ? locals.EntityCount : 0;
    std::copy_n(locals.Entities, count, targets);
    for (std::uint8_t i = 0; i < count; ++i)
    {
        Target = targets[i];
        const bool stale = !Entities.IsAlive(Target);
        if (stale || (op.When != kAbilityNoCondition && !Holds(actor, slot, definition, op.When)))
        {
            Trace({ .Actor = actor.Entity, .Key = key, .Event = AbilityTraceEvent::Skipped,
                    .Admission = stale ? VerbAdmission::UnresolvedReference : VerbAdmission::Accepted, .Element = index });
            continue;
        }
        if (!Operate(actor, slot, definition, index))
        {
            Target = EntityId{};
            return false;
        }
    }
    Target = EntityId{};
    return true;
}

bool AbilityPass::Operate(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                          std::uint16_t index)
{
    const AbilityOp& op = definition.Ops[index];
    const AbilityRecord& record = actor.Record(slot);
    const AbilityActivationKey key = record.Key;
    const VerbInvocationSource source{ .Producer = actor.Entity, .Instigator = record.Instigator, .Tick = Now.TickIndex };
    AbilityTraceRecord traced{ .Actor = actor.Entity, .Key = key, .Event = AbilityTraceEvent::Operated,
                               .Phase = record.Phase, .Element = index };

    const auto fill = [&](const std::vector<AbilityValue>& values) {
        Inputs.resize(values.size());
        for (std::size_t i = 0; i < values.size(); ++i)
            Evaluate(actor, slot, definition, values[i], Inputs[i]);
    };

    switch (op.Kind)
    {
    case AbilityOpKind::Invoke:
        fill(op.Inputs);
        traced.Admission = Host.Verbs != nullptr ? Host.Verbs->Invoke(op.Binding, Inputs, source).Status
                                                 : VerbAdmission::Unavailable;
        Trace(traced);
        return Live(actor, slot);
    case AbilityOpKind::Lease:
    {
        fill(op.Inputs);
        const LeaseOwner owner = OwnerOf(actor, slot, static_cast<std::uint8_t>(index));
        traced.Admission = Host.Verbs == nullptr ? VerbAdmission::Unavailable
                         : actor.Record(slot).LeaseCount == kAbilityRecordLeaseCapacity
                             ? VerbAdmission::QueueFull
                             : Host.Verbs->AcquireLease(op.Binding, Inputs, owner, source).Status;
        Trace(traced);
        if (!Live(actor, slot))
            return false;
        if (traced.Admission != VerbAdmission::Accepted)
        {
            End(actor, slot, AbilityEndReason::LeaseRefused, index);
            return false;
        }
        AbilityRecord& holder = actor.Record(slot);
        holder.Leases[holder.LeaseCount++] = AbilityHeldLease{ op.Binding.Verb, static_cast<std::uint8_t>(index) };
        return true;
    }
    case AbilityOpKind::Query:
    {
        fill(op.Arguments);
        const AuthoredQueryStatus status = Host.Queries != nullptr
            ? Host.Queries->Evaluate(op.Query, Inputs, Answer)
            : AuthoredQueryStatus::Unbound;
        Write(actor, slot, definition, op.Local, Answer, status == AuthoredQueryStatus::Value);
        traced.Admission = status == AuthoredQueryStatus::Value ? VerbAdmission::Accepted : VerbAdmission::Unavailable;
        Trace(traced);
        return Live(actor, slot);
    }
    case AbilityOpKind::Set:
        Evaluate(actor, slot, definition, op.From, Answer);
        Write(actor, slot, definition, op.Local, Answer, true);
        return true;
    case AbilityOpKind::Commit:
        if (Commit(actor, slot, definition, op.Groups).Paid || op.ContinueOnFail)
            return Live(actor, slot);
        End(actor, slot, AbilityEndReason::CommitFailed, index);
        return false;
    case AbilityOpKind::Cue:
    {
        traced.Event = AbilityTraceEvent::Cue;
        traced.Cue = op.Cue;
        CuePlace place;
        if (op.At)
        {
            Evaluate(actor, slot, definition, *op.At, Answer);
            place = PlaceOf(Answer);
        }
        traced.At = place.Point;
        AbilityRecord& running = actor.Record(slot);
        if (op.Pulsed)
        {
            running.LastPoint = place.Point;
            running.LastNormal = place.Normal;
            running.LastTarget = place.Target;
        }
        else if (AbilityCueLog* log = Entities.TryGet<AbilityCueLog>(actor.Entity))
            (void)AppendAbilityCue(*log, { .Cue = op.Cue, .Activation = running.Sequence, .Op = index,
                                           .Tick = Now.TickIndex, .Point = place.Point, .Normal = place.Normal,
                                           .Target = place.Target });
        Trace(traced);
        return true;
    }
    }
    return true;
}

void AbilityPass::Write(const AbilityActor& actor, std::size_t slot, const AbilityDefinition& definition,
                        std::uint8_t local, const AuthoredValue& answer, bool answered)
{
    if (local == kAbilityNoLocal)
        return;
    AbilityRecordLocals& locals = actor.Locals(slot);
    AbilityLocalValue& value = locals.Values[local];
    value = AbilityLocalValue{};
    const AbilityLocalType type = definition.Locals[local].Type;
    if (type == AbilityLocalType::Entities)
        locals.EntityCount = 0;
    if (!answered)
        return;

    const auto vector = [](const AuthoredValue& from, Vec3d& to) {
        AuthoredVectorValue read;
        if (!from.TryGetVector(read))
            return false;
        to = Vec3d(static_cast<float>(read.Components[0]), static_cast<float>(read.Components[1]),
                   static_cast<float>(read.Components[2]));
        return true;
    };
    bool truth = false;
    std::int64_t whole = 0;
    switch (type)
    {
    case AbilityLocalType::Bool:
        value.Set = answer.TryGetBool(truth);
        value.Number = truth ? 1.0 : 0.0;
        break;
    case AbilityLocalType::Int:
    case AbilityLocalType::Float:
        if (answer.TryGetInt(whole))
        {
            value.Number = static_cast<double>(whole);
            value.Set = true;
        }
        else
            value.Set = answer.TryGetFloat(value.Number);
        break;
    case AbilityLocalType::Vec3:
        value.Set = vector(answer, value.Point);
        break;
    case AbilityLocalType::Entity:
        value.Set = answer.TryGetEntity(value.Entity);
        break;
    case AbilityLocalType::Hit:
    {
        // A hit is a record of {hit, entity, point, normal} in the query's declared order.
        const std::span<const AuthoredValue> fields = answer.Children();
        if (fields.size() < 4)
            break;
        value.Set = fields[0].TryGetBool(value.Hit);
        (void)fields[1].TryGetEntity(value.Entity);
        (void)vector(fields[2], value.Point);
        (void)vector(fields[3], value.Normal);
        break;
    }
    case AbilityLocalType::Entities:
        for (const AuthoredValue& element : answer.Children())
        {
            if (locals.EntityCount == kAbilityEntityListCapacity)
                break;
            EntityId entity;
            if (element.TryGetEntity(entity))
                locals.Entities[locals.EntityCount++] = entity;
        }
        value.Set = true;
        break;
    }
}

void AbilityPass::Sweep(const AbilityActor& actor)
{
    for (std::size_t slot = 0; slot < kAbilityRecordCapacity; ++slot)
    {
        if (!Live(actor, slot))
            continue;
        AbilityRecord& record = actor.Record(slot);
        const AbilityDefinition* definition = DefinitionOf(record);
        if (definition == nullptr)
        {
            End(actor, slot, AbilityEndReason::HotReload);
            continue;
        }
        if (record.Revision != Abilities->Revision(record.Key.Ability))
        {
            if (definition->CancelOnReload || record.Phase >= definition->Phases.size())
            {
                End(actor, slot, AbilityEndReason::HotReload);
                continue;
            }
            record.Revision = Abilities->Revision(record.Key.Ability);
        }
        const AbilityGrants* grants = actor.Grants();
        if (grants == nullptr || grants->Find(record.Grant) == nullptr)
        {
            End(actor, slot, AbilityEndReason::GrantRevoked);
            continue;
        }
        if (record.TriggerAction != kAbilityNoTrigger && record.TriggerSource != actor.Source)
        {
            End(actor, slot, AbilityEndReason::InputLost);
            continue;
        }
        std::uint16_t failing = kAbilityNoCondition;
        if (!AllHold(actor, slot, *definition, definition->Continuation, failing)
            || !AllHold(actor, slot, *definition, definition->Phases[record.Phase].Continue, failing))
        {
            End(actor, slot, AbilityEndReason::ContinuationFailed, failing);
            continue;
        }
        for (const std::uint16_t trigger : definition->CancelOn)
        {
            if (Holds(actor, slot, *definition, trigger))
            {
                const bool lost = definition->Conditions[trigger].Kind == AbilityConditionKind::TargetLost;
                End(actor, slot, lost ? AbilityEndReason::TargetLost : AbilityEndReason::CancelTriggered, trigger);
                break;
            }
        }
    }
}

void AbilityPass::Advance(const AbilityActor& actor)
{
    for (std::size_t slot = 0; slot < kAbilityRecordCapacity; ++slot)
    {
        if (!Live(actor, slot))
            continue;
        const AbilityDefinition* definition = DefinitionOf(actor.Record(slot));
        if (definition == nullptr)
            continue;
        const AbilityPhase& phase = definition->Phases[actor.Record(slot).Phase];
        for (std::uint32_t pulses = 0; phase.Pulses && pulses < StepBudget && Live(actor, slot)
             && PhaseTicks(actor, actor.Record(slot), *definition) >= actor.Record(slot).NextPulse;
             ++pulses)
            Pulse(actor, slot, *definition);
        Run(actor, slot);
    }
}

void AbilityPass::Linger(const AbilityActor& actor)
{
    for (std::size_t slot = 0; slot < kAbilityRecordCapacity; ++slot)
    {
        AbilityRecord& record = actor.Record(slot);
        if (record.State == AbilityRecordState::Lingering && Now.TickIndex >= record.EndTick + LingerTicks)
        {
            record = AbilityRecord{};
            actor.Locals(slot) = AbilityRecordLocals{};
        }
    }
}
