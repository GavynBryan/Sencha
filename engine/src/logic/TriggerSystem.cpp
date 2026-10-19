#include <algorithm>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <authored/AuthoredArgumentBinding.h>
#include <authored/WorldVocabulary.h>
#include <cmath>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <limits>
#include <logic/TriggerSystem.h>
#include <world/SimulationAuthority.h>
#include <world/identity/PersistentEntityIndex.h>
#include <world/transform/TransformComponents.h>

namespace
{
bool Participates(const World &world, EntityId entity, const StoragePartitionSet &partitions)
{
    return world.IsAlive(entity) && partitions.Contains(world.GetEntityPartition(entity));
}
AuthoredValue Direction(Vec3d value)
{
    return AuthoredValue::Vector({{value.X, value.Y, value.Z, 0}, 3});
}
} // namespace

TriggerSystem::TriggerSystem(VerbDispatcher &verbs, const AuthoredQueryDispatcher &queries, const AssetRegistry &assets,
                             const DataAssetCache &dataAssets, Logger &log)
    : Verbs(verbs), Queries(queries), Assets(assets), DataAssets(dataAssets), Log(log)
{
    SetLimits(PairLimit, CommandLimit);
    Inputs.reserve(7);
}
void TriggerSystem::SetLimits(std::size_t pairs, std::size_t commands)
{
    PairLimit = std::clamp<std::size_t>(pairs, 1, 65536);
    CommandLimit = std::clamp<std::size_t>(commands, 1, 65536);
    States.reserve(PairLimit);
    Batch.reserve(PairLimit * 2);
}
TriggerSystem::State *TriggerSystem::FindState(SensorPair pair, TriggerProgramHandle program)
{
    auto found = std::ranges::lower_bound(States, pair, {}, &State::Pair);
    if (found != States.end() && found->Pair == pair)
    {
        if (found->Program != program)
            *found = State{.Pair = pair, .Program = program};
        return &*found;
    }
    if (States.size() >= PairLimit)
    {
        ++Counts.Overflowed;
        return nullptr;
    }
    return &*States.insert(found, State{.Pair = pair, .Program = program});
}
void TriggerSystem::FillInputs(std::span<const TriggerInput> inputs, const SensorOccurrence &occurrence,
                               const World &world)
{
    Quatf rotation = Quatf::Identity();
    if (world.IsRegistered<WorldTransform>())
        if (const auto *transform = world.TryGet<WorldTransform>(occurrence.Pair.Sensor))
            rotation = transform->Value.Rotation;
    Inputs.clear();
    for (const auto input : inputs)
        switch (input)
        {
        case TriggerInput::Trigger:
            Inputs.push_back(AuthoredValue::Entity(occurrence.Pair.Sensor));
            break;
        case TriggerInput::Activator:
            Inputs.push_back(AuthoredValue::Entity(occurrence.Pair.Activator));
            break;
        case TriggerInput::Event:
            Inputs.push_back(AuthoredValue::Enum(occurrence.Transition == SensorTransition::Enter  ? "Enter"
                                                 : occurrence.Transition == SensorTransition::Exit ? "Exit"
                                                                                                   : "Stay"));
            break;
        case TriggerInput::Tick:
            Inputs.push_back(AuthoredValue::Int(static_cast<std::int64_t>(
                std::min(occurrence.Tick, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))));
            break;
        case TriggerInput::Up:
            Inputs.push_back(Direction(rotation.RotateVector(Vec3d(0, 1, 0))));
            break;
        case TriggerInput::Forward:
            Inputs.push_back(Direction(rotation.RotateVector(Vec3d(0, 0, -1))));
            break;
        case TriggerInput::Right:
            Inputs.push_back(Direction(rotation.RotateVector(Vec3d(1, 0, 0))));
            break;
        }
}
void TriggerSystem::Failure(const SensorOccurrence &occurrence, VerbAdmission status)
{
    ++Counts.Failed;
    if (auto *trace = Verbs.Trace())
    {
        VerbTraceRecord record;
        record.Event = VerbTraceEvent::Abandoned;
        record.Status = status;
        record.Producer = occurrence.Pair.Sensor;
        record.Instigator = occurrence.Pair.Activator;
        trace->Record(record);
    }
}
void TriggerSystem::Execute(const SensorOccurrence &observed, FixedLogicContext &context, std::size_t &remaining)
{
    const World &world = context.Entities;
    if (!Participates(world, observed.Pair.Sensor, context.Partitions) ||
        !Participates(world, observed.Pair.Activator, context.Partitions))
        return;
    const auto *component = world.TryGet<Trigger>(observed.Pair.Sensor);
    if (component == nullptr || !component->Enabled)
        return;
    const Trigger trigger = *component;
    Errors.clear();
    if (!ValidateTriggerActivation(trigger, Errors))
    {
        Failure(observed, VerbAdmission::InvalidArguments);
        return;
    }
    auto *pair = FindState(observed.Pair, trigger.Program);
    if (pair == nullptr)
    {
        Failure(observed, VerbAdmission::QueueFull);
        return;
    }
    SensorOccurrence occurrence = observed;
    if (occurrence.Transition == SensorTransition::Stay && !pair->Active)
        occurrence.Transition = SensorTransition::Enter;
    pair->Active = occurrence.Transition != SensorTransition::Exit;
    const bool matches = (trigger.Event == TriggerEvent::Enter && occurrence.Transition == SensorTransition::Enter) ||
                         (trigger.Event == TriggerEvent::Exit && occurrence.Transition == SensorTransition::Exit) ||
                         (trigger.Event == TriggerEvent::Stay && pair->Active && Seconds >= pair->NextStay);
    if (!matches)
        return;
    pair->NextStay = Seconds + trigger.StayInterval;
    const auto key = trigger.PerActivator ? observed.Pair : SensorPair{observed.Pair.Sensor, {}};
    auto *policy = FindState(key, trigger.Program);
    if (policy == nullptr)
    {
        Failure(observed, VerbAdmission::QueueFull);
        return;
    }
    if (Seconds < policy->NextAllowed || (trigger.Once && policy->OnceFired))
    {
        ++Counts.Gated;
        return;
    }
    auto environment = MakeVerbBindingEnvironment(world);
    environment.Assets = &Assets;
    environment.DataAssets = &DataAssets;
    auto program =
        TriggerProgramsOf(context.Entities).Resolve(observed.Pair.Sensor, trigger.Program, world, environment, Errors);
    for (const auto &error : Errors)
        Log.Error("{}", error);
    if (program == nullptr)
    {
        Failure(observed, VerbAdmission::UnresolvedBinding);
        return;
    }
    for (const auto id : program->Eligibility)
        if (!world.HasComponent(observed.Pair.Activator, id))
        {
            ++Counts.Gated;
            return;
        }
    for (const auto &condition : program->Conditions)
    {
        FillInputs(condition.Inputs, occurrence, world);
        const auto resolved =
            ResolveAuthoredArguments(condition.Binding.Constants, condition.Binding.Inputs, Inputs,
                                     world.TryGetResource<PersistentEntityIndex>(), &DataAssets, QueryArguments);
        if (resolved != VerbAdmission::Accepted)
        {
            Failure(observed, resolved);
            return;
        }
        AuthoredValue answer;
        bool passed = false;
        const auto status = Queries.Evaluate(condition.Binding.Query, QueryArguments.Values(), answer);
        if (status != AuthoredQueryStatus::Value || !answer.TryGetBool(passed))
        {
            Failure(observed, VerbAdmission::Unavailable);
            return;
        }
        if (!passed)
        {
            ++Counts.Gated;
            return;
        }
    }
    if (program->Commands.size() > remaining)
    {
        ++Counts.Overflowed;
        Failure(observed, VerbAdmission::QueueFull);
        return;
    }
    remaining -= program->Commands.size();
    // State insertion can move rows; no pointer survives the last insertion.
    policy = FindState(key, trigger.Program);
    policy->NextAllowed = Seconds + trigger.Cooldown;
    policy->OnceFired = true;
    for (const auto &command : program->Commands)
    {
        if (!Participates(world, observed.Pair.Sensor, context.Partitions) ||
            !Participates(world, observed.Pair.Activator, context.Partitions))
            break;
        const auto *current = world.TryGet<Trigger>(observed.Pair.Sensor);
        if (current == nullptr || !current->Enabled || current->Program != trigger.Program)
            break;
        FillInputs(command.Inputs, occurrence, world);
        const auto result = Verbs.Invoke(
            command.Binding, Inputs,
            {.Producer = observed.Pair.Sensor, .Instigator = observed.Pair.Activator, .Tick = context.Time.TickIndex});
        if (!result.Accepted())
        {
            ++Counts.Failed;
            break;
        }
        ++Counts.Executed;
    }
}
void TriggerSystem::FixedLogic(FixedLogicContext &context)
{
    if (context.Time.TickIndex <= LastLogic)
        return;
    LastLogic = context.Time.TickIndex;
    if (std::isfinite(context.Time.DeltaSeconds) && context.Time.DeltaSeconds > 0)
        Seconds += context.Time.DeltaSeconds;
    const World &world = context.Entities;
    auto environment = MakeVerbBindingEnvironment(world);
    environment.Assets = &Assets;
    environment.DataAssets = &DataAssets;
    Errors.clear();
    TriggerProgramsOf(context.Entities).Prepare(world, environment, Errors);
    for (const auto &error : Errors)
        Log.Error("{}", error);
    const auto *observations = world.TryGetResource<SensorOverlaps>();
    if (observations == nullptr)
        return;
    std::erase_if(States, [&](const State &state) {
        if (!Participates(world, state.Pair.Sensor, context.Partitions) ||
            (state.Pair.Activator.IsValid() && !Participates(world, state.Pair.Activator, context.Partitions)))
            return true;
        const auto *trigger = world.TryGet<Trigger>(state.Pair.Sensor);
        return trigger == nullptr || !trigger->Enabled || trigger->Program != state.Program ||
               (state.Pair.Activator.IsValid() && !state.Active && (!trigger->Once || !trigger->PerActivator) &&
                Seconds >= state.NextAllowed);
    });
    Batch.clear();
    bool overflow = false;
    for (const auto &occurrence : observations->Occurrences())
    {
        if (occurrence.Tick <= LastSample || occurrence.Tick >= context.Time.TickIndex)
            continue;
        if (Batch.size() == PairLimit * 2)
            overflow = true;
        else
            Batch.push_back(occurrence);
    }
    if (!Batch.empty())
        LastSample = Batch.back().Tick;
    if (!IsSimulationAuthority(world))
    {
        States.clear();
        return;
    }
    if (Batch.empty())
        return;
    if (overflow)
    {
        ++Counts.Overflowed;
        Log.Error("trigger sample exceeds {} occurrences; whole sample refused", PairLimit * 2);
        for (auto &state : States)
            if (state.Pair.Activator.IsValid())
                state.Active = std::ranges::binary_search(observations->Pairs(), state.Pair);
        return;
    }
    auto remaining = CommandLimit;
    for (const auto &occurrence : Batch)
        Execute(occurrence, context, remaining);
}
void TriggerSystem::ZoneResidency(ZoneResidencyContext &context)
{
    for (const auto &change : context.Changes)
    {
        if (change.Kind != ZoneResidencyChangeKind::Detaching && !(change.Previous.Logic && !change.Current.Logic) &&
            !(change.Previous.Physics && !change.Current.Physics))
            continue;
        std::erase_if(States, [&](const State &state) {
            return !context.Entities.IsAlive(state.Pair.Sensor) ||
                   context.Entities.GetEntityPartition(state.Pair.Sensor) == change.Partition ||
                   (state.Pair.Activator.IsValid() &&
                    (!context.Entities.IsAlive(state.Pair.Activator) ||
                     context.Entities.GetEntityPartition(state.Pair.Activator) == change.Partition));
        });
    }
}
void TriggerSystem::Shutdown()
{
    Batch.clear();
    States.clear();
    LastLogic = LastSample = 0;
    Seconds = 0;
}
TriggerSystem &RegisterTriggerSystem(EngineSchedule &schedule, World &world, VerbDispatcher &verbs,
                                     const AuthoredQueryDispatcher &queries, const AssetRegistry &assets,
                                     const DataAssetCache &dataAssets, ConsoleRegistry &console,
                                     LoggingProvider &logging)
{
    (void)TriggerProgramsOf(world);
    auto &system =
        schedule.Register<TriggerSystem>(verbs, queries, assets, dataAssets, logging.GetLogger<TriggerSystem>());
    (void)console.RegisterCVar({
        .Name = "logic.trigger.command_budget",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = std::int64_t{4096},
        .CurrentValue = std::int64_t{4096},
        .Flags = CVarFlags::None,
        .Help =
            "Maximum trigger command attempts per tick; whole activations exceeding the remaining budget are refused.",
        .Source = {"engine"},
        .Min = 1.0,
        .Max = 65536.0,
        .OnChange =
            [&system](const CVarChangeContext &change) {
                system.SetLimits(system.PairCapacity(),
                                 static_cast<std::size_t>(std::get<std::int64_t>(change.NewValue)));
            },
    });
    (void)console.RegisterCVar({
        .Name = "logic.trigger.pair_capacity",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = std::int64_t{4096},
        .CurrentValue = std::int64_t{4096},
        .Flags = CVarFlags::None,
        .Help = "Maximum retained trigger states; new pairs are refused without evicting cooldown or once history.",
        .Source = {"engine"},
        .Min = 1.0,
        .Max = 65536.0,
        .OnChange =
            [&system](const CVarChangeContext &change) {
                system.SetLimits(static_cast<std::size_t>(std::get<std::int64_t>(change.NewValue)),
                                 system.CommandBudget());
            },
    });
    return system;
}
