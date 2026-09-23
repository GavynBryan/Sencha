#pragma once

#include "ArenaScoreboard.h"

#include <authored/AuthoredAnnotations.h>
#include <authored/VerbInvocation.h>
#include <core/metadata/EnumSchema.h>
#include <ecs/EntityId.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

class AuthoredEventDispatcher;
class EngineSchedule;
class Logger;
class VerbDispatcher;
class World;
struct FixedLogicContext;

// The template's worked example of an authored API: one verb, the
// scoreboard's queryable members, and one event. The score is replicated and
// written only by the simulation authority.

enum class ArenaSide : std::uint8_t
{
    Red,
    Blue,
};

template<>
struct EnumSchema<ArenaSide>
{
    static constexpr std::array Values = {
        EnumValue{ ArenaSide::Red, "red", "Red" },
        EnumValue{ ArenaSide::Blue, "blue", "Blue" },
    };
};

// Published once per applied award, from the match entity.
struct SENCHA_EVENT("arena.score_changed")
       SENCHA_EVENT_SOURCE(ArenaScoreboard)
       SENCHA_LABEL("Score changed")
       SENCHA_CATEGORY("Arena")
ArenaScoreChangedEvent
{
    SENCHA_FIELD("side")
    ArenaSide Side = ArenaSide::Red;

    SENCHA_FIELD("amount")
    std::int64_t Amount = 0;

    SENCHA_FIELD("instigator")
    EntityId Instigator{};
};

// Queues awards; ArenaScoreSystem applies them at fixed logic, in admission
// order, where this process is the simulation authority. Bounded; overflow is
// refused.
class ArenaScoreOperation
{
public:
    // `source`, when given, must still be alive and simulated at the drain.
    SENCHA_VERB("arena.award_score")
    SENCHA_LABEL("Award score")
    SENCHA_DESCRIPTION("Adds points to one side's score at the next fixed tick.")
    SENCHA_CATEGORY("Arena")
    VerbAdmission AwardScore(const VerbInvocation& invocation,
                             SENCHA_ARG("Side") ArenaSide side,
                             SENCHA_ARG("Amount") SENCHA_RANGE(1, 1000) std::int64_t amount = 1,
                             SENCHA_ARG("Source") std::optional<EntityId> source = std::nullopt);

    struct Award
    {
        InvocationId Id;
        EntityId Instigator;
        ArenaSide Side = ArenaSide::Red;
        std::int64_t Amount = 0;
        EntityId Source;
    };

    static constexpr std::size_t kCapacity = 32;

    std::vector<Award> Queue;
};

void DeclareArenaVocabulary(World& world);

class ArenaScoreSystem
{
public:
    // `events` may be null.
    ArenaScoreSystem(ArenaScoreOperation& operation, VerbDispatcher& dispatcher,
                     AuthoredEventDispatcher* events, Logger& log);

    void FixedLogic(FixedLogicContext& ctx);
    void Shutdown();

private:
    ArenaScoreOperation& Operation;
    VerbDispatcher& Dispatcher;
    AuthoredEventDispatcher* Events;
    Logger& Log;
    std::vector<ArenaScoreOperation::Award> Batch;
};

void RegisterArenaScoreSystem(EngineSchedule& schedule, ArenaScoreOperation& operation,
                              VerbDispatcher& dispatcher, AuthoredEventDispatcher* events,
                              Logger& log);

#if !defined(SENCHA_CODEGEN)
#  include <src/ArenaScore.sencha.h>
#endif
