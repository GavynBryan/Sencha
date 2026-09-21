#include "ArenaScore.h"
#include "ArenaScoreboard.h"

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <authored/WorldVocabulary.h>
#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>

#include <string>
#include <utility>

VerbDefinition MakeArenaAwardScoreDefinition()
{
    VerbDefinition definition;
    definition.Name = std::string(kArenaAwardScoreVerb);
    definition.DisplayName = "Award score";
    definition.Description = "Adds points to one side's score at the next fixed tick.";
    definition.Category = "Arena";

    DataFieldSchema side;
    side.Key = "Side";
    side.DisplayName = "Side";
    side.Kind = DataFieldKind::Enum;
    DataEnumChoice red;
    red.Value = "red";
    red.DisplayName = "Red";
    DataEnumChoice blue;
    blue.Value = "blue";
    blue.DisplayName = "Blue";
    side.EnumChoices = { std::move(red), std::move(blue) };

    DataFieldSchema amount;
    amount.Key = "Amount";
    amount.DisplayName = "Amount";
    amount.Kind = DataFieldKind::Int;
    amount.Numeric.Minimum = 1.0;
    amount.Numeric.Maximum = 1000.0;
    amount.Default = std::int64_t{ 1 };

    // Who is awarding, when something placed is. Optional, so a menu entry
    // supplies nothing and a relay supplies itself; when present it has to be
    // alive and simulated for the award to count.
    DataFieldSchema source;
    source.Key = "Source";
    source.DisplayName = "Source";
    source.Kind = DataFieldKind::Optional;
    source.Required = false;
    DataFieldSchema entity;
    entity.Kind = DataFieldKind::Entity;
    source.Children.push_back(std::move(entity));

    definition.Arguments.Children = { std::move(side), std::move(amount), std::move(source) };
    return definition;
}

void DeclareArenaVerbs(World& world)
{
    VerbRegistry* verbs = FindVerbRegistry(world);
    if (verbs == nullptr)
        return;
    VerbRegistrationScope scope(*verbs, "arena");
    (void)scope.Declare(MakeArenaAwardScoreDefinition());
    // Unchecked here on purpose: the host reads the catalog's installation
    // errors after the hook returns and refuses to start on any of them.
    (void)scope.Commit();
}

VerbAdmission ArenaScoreOperation::Invoke(const VerbInvocation& invocation)
{
    if (Queue.size() >= kCapacity)
        return VerbAdmission::QueueFull;

    Award award;
    award.Id = invocation.Id;
    award.Instigator = invocation.Instigator;

    std::string_view side;
    if (!invocation.Arguments->TryGetEnum(kArenaAwardSideSlot, side)
        || !invocation.Arguments->TryGetInt(kArenaAwardAmountSlot, award.Amount))
    {
        return VerbAdmission::InvalidArguments;
    }
    award.Side = side == "blue" ? ArenaSide::Blue : ArenaSide::Red;
    // Absent is a value here; anything else must be an entity.
    const VerbValue& source = invocation.Arguments->At(kArenaAwardSourceSlot);
    if (!source.IsNone() && !source.TryGetEntity(award.Source))
        return VerbAdmission::InvalidArguments;

    Queue.push_back(award);
    return VerbAdmission::Accepted;
}

ArenaScoreSystem::ArenaScoreSystem(ArenaScoreOperation& operation, VerbDispatcher& dispatcher,
                                   Logger& log)
    : Operation(operation)
    , Dispatcher(dispatcher)
    , Log(log)
{
}

void ArenaScoreSystem::FixedLogic(FixedLogicContext& ctx)
{
    if (Operation.Queue.empty())
        return;
    Batch.clear();
    std::swap(Batch, Operation.Queue);

    World& world = ctx.Entities;

    // Only the authority writes the score; everyone else receives it as state.
    // Asked once per drain, not per award, and asked here rather than at
    // admission so a client's menu still reports Accepted -- the request was
    // taken; the answer arrives replicated.
    const bool authoritative = IsSimulationAuthority(world);

    // The match entity, made on first use where it can be made: a persistent
    // row that replication carries like any other. Structural, so it happens
    // outside any query, which a drain is.
    EntityId match;
    if (authoritative && world.IsRegistered<ArenaScoreboard>())
    {
        world.ForEachComponent<ArenaScoreboard>([&match](EntityId entity, const ArenaScoreboard&) {
            if (!match.IsValid())
                match = entity;
        });
        if (!match.IsValid())
        {
            match = world.CreateEntity(PersistentStoragePartition);
            world.AddComponent<ArenaScoreboard>(match, ArenaScoreboard{});
        }
    }

    for (const ArenaScoreOperation::Award& award : Batch)
    {
        // Validated again now, against the world as it is at the tick, not as
        // it was when the menu or the relay asked.
        const bool sourced = award.Source.IsValid();
        const bool eligible = authoritative && match.IsValid()
            && (!sourced
                || (world.IsAlive(award.Source)
                    && ctx.Partitions.Contains(world.GetEntityPartition(award.Source))));
        if (VerbTraceRing* trace = Dispatcher.Trace())
        {
            VerbTraceRecord record;
            record.Event = eligible ? VerbTraceEvent::Executed : VerbTraceEvent::Abandoned;
            record.Status = eligible ? VerbAdmission::Accepted : VerbAdmission::Refused;
            record.Id = award.Id;
            record.Producer = award.Source;
            record.Instigator = award.Instigator;
            trace->Record(record);
        }
        if (!eligible)
            continue;

        ArenaScoreboard& board = *world.TryGet<ArenaScoreboard>(match);
        (award.Side == ArenaSide::Blue ? board.Blue : board.Red) +=
            static_cast<std::int32_t>(award.Amount);
        Log.Info("ArenaGame: {} scores {} (red {}, blue {})",
                 award.Side == ArenaSide::Blue ? "blue" : "red", award.Amount, board.Red,
                 board.Blue);
    }
    Batch.clear();
}

void ArenaScoreSystem::Shutdown()
{
    Operation.Queue.clear();
    Batch.clear();
}

VerbBindingToken BindArenaScore(VerbDispatcher& dispatcher, ArenaScoreOperation& operation)
{
    return dispatcher.Bind(dispatcher.Registry().Find(kArenaAwardScoreVerb), operation);
}

void RegisterArenaScoreSystem(EngineSchedule& schedule, ArenaScoreOperation& operation,
                              VerbDispatcher& dispatcher, Logger& log)
{
    schedule.Register<ArenaScoreSystem>(operation, dispatcher, log);
}
