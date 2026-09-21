#pragma once

#include <authored/VerbDispatcher.h>
#include <authored/VerbRegistry.h>
#include <ecs/EntityId.h>

#include <cstdint>
#include <string_view>
#include <vector>

class EngineSchedule;
class Logger;
class World;
struct FixedLogicContext;

//=============================================================================
// Arena score
//
// The template's worked example of a game-defined authored operation: one verb,
// declared once, bound once, reached from two producers with one schema. The
// shell's menu awards a point through a binding in the game's own asset; a
// relay placed in the level awards one when a native path activates it. Neither
// producer knows the other exists, and neither names the game's code.
//
// The score lives on a replicated component (ArenaScoreboard), so it is state
// every peer sees and a late joiner reconstructs from the snapshot. Awarding is
// an event that changes it, applied only where this process is the simulation
// authority: a client asking awards nothing locally, and the result arrives
// as state. A relay names an entity anyway -- its own -- so the operation
// validates a typed entity input at execution, which is the one thing the
// relay proof is for.
//=============================================================================

inline constexpr std::string_view kArenaAwardScoreVerb = "arena.award_score";

// Which side a point goes to. Two on purpose: the smallest thing an enum
// argument can select between.
enum class ArenaSide : std::uint8_t
{
    Red,
    Blue,
};

// The verb's contract, in one place: the declaration in OnRegisterVocabulary
// and the slot indices the operation reads both come from here.
[[nodiscard]] VerbDefinition MakeArenaAwardScoreDefinition();
inline constexpr std::size_t kArenaAwardSideSlot = 0;
inline constexpr std::size_t kArenaAwardAmountSlot = 1;
inline constexpr std::size_t kArenaAwardSourceSlot = 2;

// Declares the game's verbs into one World's catalog. Registration only.
void DeclareArenaVerbs(World& world);

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
    VerbAdmission Invoke(const VerbInvocation& invocation);

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

class ArenaScoreSystem
{
public:
    ArenaScoreSystem(ArenaScoreOperation& operation, VerbDispatcher& dispatcher, Logger& log);

    void FixedLogic(FixedLogicContext& ctx);
    void Shutdown();

private:
    ArenaScoreOperation& Operation;
    VerbDispatcher& Dispatcher;
    Logger& Log;
    std::vector<ArenaScoreOperation::Award> Batch;
};

// Binds the operation behind the verb and registers the system that drains it.
// The token is the caller's to keep and give back before the operation goes.
[[nodiscard]] VerbBindingToken BindArenaScore(VerbDispatcher& dispatcher,
                                              ArenaScoreOperation& operation);
void RegisterArenaScoreSystem(EngineSchedule& schedule, ArenaScoreOperation& operation,
                              VerbDispatcher& dispatcher, Logger& log);
