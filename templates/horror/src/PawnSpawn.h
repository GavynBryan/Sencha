#pragma once

#include <app/BodySpawns.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <math/Vec.h>

#include <optional>

class Engine;
class Logger;
class World;
struct CompiledHorrorSettings;
struct FrameUpdateContext;
struct ZoneResidencyContext;

// Where a body comes from and who ends up driving it.
//
// The engine owns who a participant is, which of them this process provides a
// body for, what happens when one leaves, and -- through BodySpawns
// -- a prefab request from the moment it is asked for until its group is handed
// over or cleaned up. What is here is the game's half: which prefab, where it
// stands, when there is somewhere for it to stand, and pointing this machine's
// camera at whatever it turned out to be driving.

// Where a player goes when the level does not say. Above the origin rather than
// on it, so a body lands on a floor at zero instead of inside it.
inline constexpr Vec3d kDefaultPlayerStart{ 0.0f, 2.0f, 0.0f };

//=============================================================================
// PlayContentPartition
//
// Which storage partition the loaded play content occupies, published when a
// load finishes. Until one exists there is nowhere to put a pawn.
//
// This is what lets the spawn be a decision taken once content is ready,
// rather than a side effect of whichever load callback happened to run --
// which is what made the answer depend on whether a join beat a map load.
//=============================================================================
struct PlayContentPartition
{
    std::optional<StoragePartitionId> Value;
};

// Where a level says players begin, or none when it does not say.
[[nodiscard]] std::optional<Vec3d> FindPlayerStart(
    const World& world, std::optional<StoragePartitionId> partition);

void PublishPlayContent(World& world,
                        std::optional<StoragePartitionId> partition);

// Content has arrived, so anybody admitted before it can have a body now.
void RequestBodiesForWaitingParticipants(Engine& engine);

// Which prefab a participant's body is and where it stands: the level's player
// start, offset by peer so two players do not arrive inside each other. None
// when there is no content to stand in or no prefab configured, and each of
// those is said out loud.
[[nodiscard]] std::optional<BodySpawnRequest> ChoosePawnSpawn(
    const World& world, EntityId participant,
    const CompiledHorrorSettings* settings, Logger& log);

// A pawn landed for `participant` and is about to be handed to the lifecycle.
void PreparePawn(World& world, EntityId participant, EntityId root, Logger& log);

//=============================================================================
// SessionPlayerSystem
//
// Presents whichever body this machine ended up driving.
//
// Where that body came from is not this system's question any more. Who is a
// participant, which of them this process provides and which arrive replicated,
// and what happens to a body when its player leaves are all decided by the
// engine, at the points where the session role is actually known. What is left
// here is the half that is genuinely a game's: the camera, and giving a pawn
// this machine is about to simulate the rest of its body.
//=============================================================================
struct SessionPlayerSystem
{
    Engine* Owner = nullptr;
    Logger* Log = nullptr;

    void FrameUpdate(FrameUpdateContext& ctx);

private:
    void FollowLocalControl(World& world);

    // The pawn this machine was last told to drive, so taking up a new one is
    // an edge rather than something re-derived every frame.
    EntityId Followed;
};

//=============================================================================
// SpawnSettlementSystem
//
// Decides when there is somewhere for a body to go, and lets the engine's
// prefab request book settle what landed. The book owns every request from
// ask to handoff; this system owns when to ask and when to give up.
//=============================================================================
struct SpawnSettlementSystem
{
    Engine* Owner = nullptr;
    Logger* Log = nullptr;
    BodySpawns* Bodies = nullptr;

    // Where a pawn belongs, learned from the level arriving rather than from
    // having loaded it. The engine loads; this decides that a loaded play zone
    // is somewhere a player's body can go, and asks for one. When it goes,
    // whatever was still being asked for goes with it: a request made for
    // departed content must not land in the next map.
    void ZoneResidency(ZoneResidencyContext& ctx);

    void FrameUpdate(FrameUpdateContext& ctx);
};
