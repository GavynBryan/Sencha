#pragma once

#include <core/console/ConsoleTypes.h>
#include <ecs/EntityId.h>
#include <net/NetProtocol.h>
#include <net/NetSession.h>

#include <cstdint>

class Engine;
class Logger;
class World;
struct CompiledGameSettings;
struct FixedLogicContext;
struct NetMessageContext;

// Getting into and out of a turret: the request a client sends, the rules the
// authority answers it by, and the console paths that reach the same answer
// without a session.
//
// The template ships this so the possession path is exercised by content every
// level has -- namely none. A game that authored its own mounts would keep the
// request and drop the placement.

// What this game says over the wire. The engine reserves everything below
// kNetFirstGamePayloadKind for itself, so a game's kinds start there and can
// never be swallowed by the dispatch that answers snapshots and commands.
//
// "Put me in that turret", or "take me out of the one I am in". One kind for
// both because the authority already knows which of them a peer means: it
// holds the record of who owns what, and a request that disagreed with it
// would be refused anyway.
inline constexpr std::uint8_t kTurretRequestKind = kNetFirstGamePayloadKind;

enum class TurretRequestOutcome : std::uint8_t
{
    Took,
    Left,
    Occupied,
    Invalid,
};

// What this participant is driving now, and what it should drive next.
//
// `driver` is the peer to hand ownership to, or an invalid peer for the
// authority's own player -- who needs none, because the authority already has
// everything ownership would deliver.
[[nodiscard]] TurretRequestOutcome ApplyTurretRequest(
    Engine& engine,
    World& world,
    EntityId participant,
    EntityId turret,
    PeerId driver);

// The authority's side of a client's request. Fails closed on an identity it
// never minted.
[[nodiscard]] bool AnswerTurretRequest(Engine& engine, Logger& log,
                                       const NetMessageContext& message);

// The client half: name the turret in terms the authority will recognise, and
// send. Nothing is decided here.
[[nodiscard]] ConsoleResult AskAuthorityForTurret(Engine& engine,
                                                  NetSession& session);

// Putting one down without getting into it, and asking for one where this
// process is the authority the request would have gone to. Authority-only,
// structurally: a client's turrets arrive replicated.
[[nodiscard]] ConsoleResult PlaceTurretHere(
    Engine& engine, const CompiledGameSettings* settings, Logger& log);
[[nodiscard]] ConsoleResult TakeTurretHere(
    Engine& engine, const CompiledGameSettings* settings, Logger& log);

//=============================================================================
// TurretAimSystem
//
// Points a turret where its driver is looking. The engine already hands a
// peer's aim to the entity that peer owns, so this reads what arrived and turns
// it into the one number that travels: no branch on who is driving, no branch
// on whether anybody is.
//=============================================================================
struct TurretAimSystem
{
    void FixedLogic(FixedLogicContext& ctx);
};
