#include <logic/TriggerSystem.h>

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <core/console/ConsoleRegistry.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/Trigger.h>
#include <logic/TriggerSequences.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
std::uint64_t TicksIn(float seconds, double tickSeconds)
{
    if (!(seconds > 0.0f) || !(tickSeconds > 0.0))
        return 0;
    // Authored seconds are floats: 0.3 s of 0.1 s ticks is three, not the four
    // its rounding error would ceil to.
    const double ticks = static_cast<double>(seconds) / tickSeconds;
    const double nearest = std::round(ticks);
    return static_cast<std::uint64_t>(std::abs(ticks - nearest) < 1.0e-3 ? nearest : std::ceil(ticks));
}

const Trigger* TriggerOf(const World& world, EntityId entity)
{
    return world.IsRegistered<Trigger>() && world.IsAlive(entity) ? world.TryGet<Trigger>(entity) : nullptr;
}

bool Participates(const World& world, const StoragePartitionSet& partitions, EntityId entity)
{
    return world.IsAlive(entity) && partitions.Contains(world.GetEntityPartition(entity));
}

const CompiledVerbSequence kEmptySequence{};
} // namespace

TriggerSystem::TriggerSystem(VerbDispatcher& verbs,
                             const AuthoredQueryDispatcher* queries,
                             const AssetRegistry* assets,
                             const DataAssetCache* dataAssets,
                             Logger& log)
    : Verbs(verbs)
    , Queries(queries)
    , Assets(assets)
    , DataAssets(dataAssets)
    , Log(log)
{
}

void TriggerSystem::SetCapacity(std::size_t activations)
{
    QueueCapacity = std::max<std::size_t>(activations, 1);
}

void TriggerSystem::SetCommandBudget(std::size_t commands)
{
    Budget = std::max<std::size_t>(commands, 1);
}

void TriggerSystem::Admit(EntityId trigger, EntityId activator)
{
    if (Queue.size() >= QueueCapacity)
    {
        if (Counts.Refused++ == 0)
            Log.Warn("trigger: activation queue full ({}); raise logic.trigger.queue_capacity", QueueCapacity);
        return;
    }
    Queue.push_back(Activation{ trigger, activator });
}

void TriggerSystem::Observe(const World& world, const SensorOverlaps& overlaps, double tickSeconds)
{
    if (overlaps.OverflowCount() != ReportedOverflows)
    {
        Counts.SensorOverflows += overlaps.OverflowCount() - ReportedOverflows;
        ReportedOverflows = overlaps.OverflowCount();
        Log.Warn("trigger: a sensor sample overflowed and was dropped; crossings in it are lost");
    }

    const std::span<const SensorOccurrence> occurrences = overlaps.Occurrences();
    if (occurrences.empty() || (SampledBefore && occurrences.front().Tick <= LastSampleTick))
        return;
    LastSampleTick = occurrences.front().Tick;
    SampledBefore = true;

    for (const SensorOccurrence& occurrence : occurrences)
    {
        const Trigger* trigger = TriggerOf(world, occurrence.Pair.Sensor);
        if (trigger == nullptr)
            continue;
        switch (trigger->Activation)
        {
        case TriggerActivation::Enter:
            if (occurrence.Transition == SensorTransition::Enter && trigger->Enabled)
                Admit(occurrence.Pair.Sensor, occurrence.Pair.Activator);
            break;
        case TriggerActivation::Exit:
            if (occurrence.Transition == SensorTransition::Exit && trigger->Enabled)
                Admit(occurrence.Pair.Sensor, occurrence.Pair.Activator);
            break;
        case TriggerActivation::Stay:
        {
            PairState& state = Pairs[occurrence.Pair];
            if (occurrence.Transition == SensorTransition::Exit)
            {
                state.Inside = false;
                break;
            }
            const bool due = !state.Inside || occurrence.Tick >= state.NextStayTick;
            state.Inside = true;
            if (!due || !trigger->Enabled)
                break;
            state.NextStayTick = occurrence.Tick + std::max<std::uint64_t>(1, TicksIn(trigger->StayIntervalSeconds, tickSeconds));
            Admit(occurrence.Pair.Sensor, occurrence.Pair.Activator);
            break;
        }
        case TriggerActivation::Interaction:
            break;
        }
    }
}

void TriggerSystem::AnswerInteractions(World& world, const SensorOverlaps* overlaps)
{
    TriggerInteractions* requests = world.TryGetResource<TriggerInteractions>();
    if (requests == nullptr || requests->Activators.empty())
        return;
    Interactions.clear();
    std::swap(Interactions, requests->Activators);
    if (overlaps == nullptr)
        return;
    for (const EntityId activator : Interactions)
        for (const SensorPair& pair : overlaps->Pairs())
        {
            if (pair.Activator != activator)
                continue;
            const Trigger* trigger = TriggerOf(world, pair.Sensor);
            if (trigger != nullptr && trigger->Activation == TriggerActivation::Interaction && trigger->Enabled)
                Admit(pair.Sensor, activator);
        }
}

void TriggerSystem::RunQueue(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    Batch.clear();
    std::swap(Batch, Queue);
    Consumed.clear();

    TriggerSequences* sequences = world.TryGetResource<TriggerSequences>();
    const std::uint64_t now = ctx.Time.TickIndex;
    std::size_t spent = 0;
    std::size_t index = 0;
    for (; index < Batch.size(); ++index)
    {
        const Activation activation = Batch[index];
        if (spent >= Budget)
            break;
        const Trigger* found = TriggerOf(world, activation.Trigger);
        if (found == nullptr || !Participates(world, ctx.Partitions, activation.Trigger)
            || !Participates(world, ctx.Partitions, activation.Activator)
            || std::ranges::find(Consumed, activation.Trigger) != Consumed.end())
        {
            ++Counts.Abandoned;
            continue;
        }
        // Copied: a command may move the trigger's row, or end it.
        const Trigger trigger = *found;
        const SensorPair cooldownKey{ activation.Trigger, trigger.CooldownScope == TriggerCooldownScope::Activator
                                                              ? activation.Activator
                                                              : EntityId{} };
        const auto cooling = Pairs.find(cooldownKey);
        if (!trigger.Enabled || (cooling != Pairs.end() && cooling->second.ReadyTick > now))
        {
            ++Counts.Gated;
            continue;
        }

        const CompiledVerbSequence* sequence = &kEmptySequence;
        if (sequences != nullptr && trigger.Sequence.IsValid())
        {
            sequence = sequences->Store.Resolve(trigger.Sequence, world, Assets, DataAssets);
            if (sequences->Store.CompileCount() != ReportedCompiles)
            {
                ReportedCompiles = sequences->Store.CompileCount();
                for (const std::string& diagnostic : sequences->Store.Diagnostics(trigger.Sequence))
                    Log.Error("trigger sequence: {}", diagnostic);
            }
            if (sequence == nullptr)
                sequence = &kEmptySequence;
        }

        const std::array<AuthoredValue, 2> ports{ AuthoredValue::Entity(activation.Activator),
                                                  AuthoredValue::Entity(activation.Trigger) };
        const SequenceRun run{
            .Entities = world,
            .Verbs = Verbs,
            .Queries = Queries,
            .Tags = world.TryGetResource<GameplayTagRegistry>(),
            .Ports = ports,
            .Source = VerbInvocationSource{ .Producer = activation.Trigger,
                                            .Instigator = activation.Activator,
                                            .Tick = now },
        };
        const SequenceOutcome outcome = Runner.Run(*sequence, run);
        spent += std::max<std::size_t>(1, sequence->Commands.size());

        if (!outcome.Held)
        {
            ++Counts.Gated;
            continue;
        }
        ++Counts.Activations;
        Counts.Invoked += outcome.Invoked;
        Counts.Failed += outcome.Failed;
        if (const std::uint64_t cooldown = TicksIn(trigger.CooldownSeconds, ctx.Time.DeltaSeconds); cooldown > 0)
            Pairs[cooldownKey].ReadyTick = now + cooldown;
        if (trigger.Once)
            Consumed.push_back(activation.Trigger);
    }

    // What the budget did not reach waits, ahead of anything admitted later.
    Queue.assign(Batch.begin() + static_cast<std::ptrdiff_t>(index), Batch.end());
    Batch.clear();

    // A consumed trigger ends, which the zone's state overlay remembers across
    // an unload, and which no reload or late join can undo.
    for (const EntityId trigger : Consumed)
        if (world.IsAlive(trigger))
            world.DestroyEntity(trigger);
}

void TriggerSystem::Forget(const World& world, std::uint64_t now)
{
    std::erase_if(Pairs, [&](const auto& entry) {
        const SensorPair& pair = entry.first;
        const PairState& state = entry.second;
        return !world.IsAlive(pair.Sensor) || (pair.Activator.IsValid() && !world.IsAlive(pair.Activator))
            || (!state.Inside && state.ReadyTick <= now);
    });
}

void TriggerSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    const SensorOverlaps* overlaps = world.TryGetResource<SensorOverlaps>();
    if (!IsSimulationAuthority(world))
    {
        // A client runs nothing and remembers nothing it would have to unlearn.
        Queue.clear();
        Pairs.clear();
        if (TriggerInteractions* requests = world.TryGetResource<TriggerInteractions>())
            requests->Activators.clear();
        if (overlaps != nullptr && !overlaps->Occurrences().empty())
        {
            LastSampleTick = overlaps->Occurrences().front().Tick;
            SampledBefore = true;
        }
        return;
    }

    if (overlaps != nullptr && world.IsRegistered<Trigger>())
        Observe(world, *overlaps, ctx.Time.DeltaSeconds);
    AnswerInteractions(world, overlaps);
    if (!Queue.empty())
        RunQueue(ctx);
    if (!Pairs.empty())
        Forget(world, ctx.Time.TickIndex);
}

void TriggerSystem::ZoneResidency(ZoneResidencyContext& ctx)
{
    const World& world = ctx.Entities;
    for (const auto& change : ctx.Changes)
    {
        if (change.Kind != ZoneResidencyChangeKind::Detaching
            && !(change.Previous.Logic && !change.Current.Logic))
            continue;
        const auto departing = [&](EntityId entity) {
            return entity.IsValid() && (!world.IsAlive(entity) || world.GetEntityPartition(entity) == change.Partition);
        };
        std::erase_if(Queue, [&](const Activation& activation) {
            return departing(activation.Trigger) || departing(activation.Activator);
        });
        std::erase_if(Pairs, [&](const auto& entry) {
            return departing(entry.first.Sensor) || departing(entry.first.Activator);
        });
    }
}

void TriggerSystem::Shutdown()
{
    Queue.clear();
    Batch.clear();
    Pairs.clear();
}

TriggerSystem& RegisterTriggerSystem(EngineSchedule& schedule,
                                     World& world,
                                     VerbDispatcher& verbs,
                                     const AuthoredQueryDispatcher* queries,
                                     const AssetRegistry* assets,
                                     const DataAssetCache* dataAssets,
                                     ConsoleRegistry& console,
                                     LoggingProvider& logging)
{
    if (world.TryGetResource<TriggerInteractions>() == nullptr)
        world.AddResource<TriggerInteractions>();
    TriggerSystem& system = schedule.Register<TriggerSystem>(verbs, queries, assets, dataAssets,
                                                             logging.GetLogger<TriggerSystem>());

    (void)console.RegisterCVar({
        .Name = "logic.trigger.queue_capacity",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(system.Capacity()),
        .CurrentValue = static_cast<std::int64_t>(system.Capacity()),
        .Flags = CVarFlags::None,
        .Help = "How many trigger activations may wait to run. Overflow refuses the activation "
                "rather than dropping one already admitted.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 65536.0,
        .OnChange = [&system](const CVarChangeContext& ctx) {
            system.SetCapacity(static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
    (void)console.RegisterCVar({
        .Name = "logic.trigger.command_budget",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = static_cast<std::int64_t>(system.CommandBudget()),
        .CurrentValue = static_cast<std::int64_t>(system.CommandBudget()),
        .Flags = CVarFlags::None,
        .Help = "How many trigger commands run in one tick. Activations past it wait for the next tick, in order.",
        .Source = { "engine" },
        .Min = 1.0,
        .Max = 65536.0,
        .OnChange = [&system](const CVarChangeContext& ctx) {
            system.SetCommandBudget(static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
        },
    });
    return system;
}
