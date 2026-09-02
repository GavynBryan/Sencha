#pragma once

#include "PlayerAvatarData.h"

#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>
#include <math/Vec.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/RuntimeWorld.h>

#include <optional>
#include <span>
#include <utility>
#include <vector>

class Engine;
class Logger;
class World;
struct FrameUpdateContext;

// Where a body comes from and who ends up driving it.
//
// The engine owns who a participant is, which of them this process provides a
// body for, and what happens when one leaves. What is here is the game's half:
// building a pawn, deciding where it stands, dressing it, and pointing this
// machine's camera at whatever it turned out to be driving.

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

//=============================================================================
// PendingSceneSpawns
//
// Scene spawns settle at the frame drain, but the participant lifecycle asks
// for a body synchronously -- so requests wait here, and the settlement
// system re-asks when one lands. Live prefab bodies are remembered so a
// reaped participant despawns its whole group, not just the root the
// lifecycle knows about.
//=============================================================================
struct PendingSceneSpawns
{
    struct PawnRequest
    {
        EntityId Participant;
        SceneSpawnId Spawn;
    };
    struct TurretRequest
    {
        SceneSpawnId Spawn;
        EntityId Possessor; // invalid = placed without a taker
    };
    std::vector<PawnRequest> Pawns;
    std::vector<TurretRequest> Turrets;
    std::vector<std::pair<EntityId, SceneSpawnId>> LiveBodies;
};

[[nodiscard]] PendingSceneSpawns& PendingSpawnsOf(World& world);

// The spawned group's root: the member without a parent.
[[nodiscard]] EntityId SpawnedGroupRoot(const World& world,
                                        std::span<const EntityId> members);

// An entity with a transform and the world transform the engine owes it.
[[nodiscard]] EntityId CreateTransformEntity(
    World& world,
    const Vec3d& position,
    StoragePartitionId partition = PersistentStoragePartition,
    const Vec3d& scale = Vec3d::One());

// Names the prefab a spawned group came from, so a peer receiving it builds
// the same body rather than a bag of replicated components.
void StampNetPrefab(World& world, EntityId root, Logger& log);

void AttachAvatarMesh(World& world, EntityId entity,
                      const ResolvedPlayerAvatar& avatar);

// A flying body with the movement columns a pawn has, for looking at a level
// that has no player to put in it.
[[nodiscard]] EntityId SpawnObserverPawn(World& world, const Vec3d& at);

// Where a level says players begin, or none when it does not say.
[[nodiscard]] std::optional<Vec3d> FindPlayerStart(
    const World& world, std::optional<StoragePartitionId> partition);

void PublishPlayContent(World& world,
                        std::optional<StoragePartitionId> partition);

// Content has arrived, so anybody admitted before it can have a body now.
void RequestBodiesForWaitingParticipants(Engine& engine);

// Points this machine's camera at whatever it is driving.
void AttachLocalPlayer(World& world, EntityId pawn, Logger& log);

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
    ResolvedPlayerAvatar Avatar;

    void FrameUpdate(FrameUpdateContext& ctx);

private:
    void DressArrivedBodies(World& world);
    void FollowLocalControl(World& world);

    // The pawn this machine was last told to drive, so taking up a new one is
    // an edge rather than something re-derived every frame.
    EntityId Followed;
};

//=============================================================================
// SpawnSettlementSystem
//
// Watches the pending scene spawns each frame, after the drain where the
// spawn service publishes. A settled pawn request re-asks the participant
// lifecycle (ProvideBody consumes the entry either way); a settled turret
// gets its runtime body, its replication stamps, and its waiting driver.
//=============================================================================
struct SpawnSettlementSystem
{
    Engine* Owner = nullptr;
    Logger* Log = nullptr;

    void FrameUpdate(FrameUpdateContext& ctx);

private:
    std::vector<EntityId> ReAskScratch;
};
