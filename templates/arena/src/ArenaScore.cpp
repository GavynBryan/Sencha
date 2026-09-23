#include "ArenaScore.h"
#include "ArenaScoreboard.h"

#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <authored/AuthoredApi.h>
#include <authored/AuthoredEventDispatcher.h>
#include <authored/VerbDispatcher.h>
#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <world/RuntimeWorld.h>
#include <world/SimulationAuthority.h>

#include <utility>

void DeclareArenaVocabulary(World& world)
{
    AuthoredVocabularyScope vocabulary(world, "arena");
    vocabulary.Declare<ArenaScoreOperation>();
    vocabulary.Declare<ArenaScoreboard>();
    vocabulary.Declare<ArenaScoreChangedEvent>();
    // The host reads the catalogs' installation errors.
    (void)vocabulary.Commit();
}

VerbAdmission ArenaScoreOperation::AwardScore(const VerbInvocation& invocation,
                                              ArenaSide side,
                                              std::int64_t amount,
                                              std::optional<EntityId> source)
{
    if (Queue.size() >= kCapacity)
        return VerbAdmission::QueueFull;

    Queue.push_back(Award{
        .Id = invocation.Id,
        .Instigator = invocation.Instigator,
        .Side = side,
        .Amount = amount,
        .Source = source.value_or(EntityId{}),
    });
    return VerbAdmission::Accepted;
}

ArenaScoreSystem::ArenaScoreSystem(ArenaScoreOperation& operation, VerbDispatcher& dispatcher,
                                   AuthoredEventDispatcher* events, Logger& log)
    : Operation(operation)
    , Dispatcher(dispatcher)
    , Events(events)
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

        // Amount is at least one, so every applied award is a change.
        if (Events != nullptr)
        {
            (void)Events->Publish(match,
                                  ArenaScoreChangedEvent{
                                      .Side = award.Side,
                                      .Amount = award.Amount,
                                      .Instigator = award.Instigator,
                                  },
                                  award.Id);
        }
    }
    Batch.clear();
}

void ArenaScoreSystem::Shutdown()
{
    Operation.Queue.clear();
    Batch.clear();
}

void RegisterArenaScoreSystem(EngineSchedule& schedule, ArenaScoreOperation& operation,
                              VerbDispatcher& dispatcher, AuthoredEventDispatcher* events,
                              Logger& log)
{
    schedule.Register<ArenaScoreSystem>(operation, dispatcher, events, log);
}
