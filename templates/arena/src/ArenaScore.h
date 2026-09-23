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

//=============================================================================
// Arena score
//
// The template's worked example of a game's authored API: one verb, one
// component whose members authored content may read, and one event -- each an
// ordinary C++ declaration with annotations, declared once and bound once.
//
// The verb is reached from two producers with one schema. The shell's menu
// awards a point through a binding in the game's own asset; a relay placed in
// the level awards one when a native path activates it. Neither producer knows
// the other exists, and neither names the game's code.
//
// The score lives on a replicated component (ArenaScoreboard), so it is state
// every peer sees and a late joiner reconstructs from the snapshot. Awarding is
// a request that changes it, applied only where this process is the simulation
// authority: a client asking awards nothing locally, and the result arrives
// as state. Applying an award is also what announces it, as
// arena.score_changed, from the entity whose scoreboard changed.
//=============================================================================

// Which side a point goes to. Two on purpose: the smallest thing an enum
// argument can select between.
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

// Announced where the score was actually changed: once per applied award, by
// the authority, from the match entity. A refused or abandoned award changed
// nothing and announces nothing.
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

    // Whoever asked for the award, when the request said.
    SENCHA_FIELD("instigator")
    EntityId Instigator{};
};

//-----------------------------------------------------------------------------
// ArenaScoreOperation
//
// Admits awards into a bounded queue; ArenaScoreSystem applies them at fixed
// logic. Deferred because the score is simulation state: an award admitted
// from a menu on the frame clock lands on the tick, once, in admission order.
//
// Contract at the drain: applied only where this process is the simulation
// authority, and only if a named source entity is still alive and in the
// tick's logic set; one naming none is applied unconditionally. Requests
// present when the drain begins are applied in admission order; one admitted
// while draining waits for the next tick. Capacity is fixed; overflow refuses
// admission. Shutdown drops the queue.
//-----------------------------------------------------------------------------
class ArenaScoreOperation
{
public:
    // `source` is who is awarding, when something placed is: a menu entry
    // supplies nothing and a relay supplies itself. When present it has to be
    // alive and simulated for the award to count.
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

// The game's authored vocabulary: the verb, the scoreboard's readable members,
// and the event. Registration only.
void DeclareArenaVocabulary(World& world);

class ArenaScoreSystem
{
public:
    // `events` may be null, and then nothing is announced.
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
