#pragma once

#include <math/Vec.h>

#include <optional>

class CharacterMoverPool;
class Engine;
class World;
class WorldPartitionRuntime;
struct FixedLogicContext;
struct FrameUpdateContext;
struct ZoneResidencyContext;

//=============================================================================
// PawnStreaming
//
// This game's streaming policy: the world stays loaded around the body this
// machine drives. The engine streams whatever the partition's focus is; this
// decides that the focus is the local player, and what to do when the player
// reaches a room that is not ready.
//
// Focus is set in the zone-residency hook, which runs after the partition
// updated this frame, so the sweep it feeds lands on the next update. That is
// one frame later than a focus set before the update would be, and it is the
// same wall clock either way; the dock sweep runs between successive focus
// positions, so a fast crossing is still a crossing.
//=============================================================================
// The focus half on its own: the partition streams around the body this
// machine drives, shaped like that body. Pure over a world and a partition so
// it is tested against a synthetic one; the system below is what calls it.
void FocusStreamingOnLocalPawn(const World& world, WorldPartitionRuntime& partition);

struct PawnStreaming
{
    Engine* Owner = nullptr;
    // Owned by the physics step, which is where characters live. Null in a
    // configuration with no physics, where the transform is all there is.
    CharacterMoverPool* Movers = nullptr;

    void ZoneResidency(ZoneResidencyContext& ctx);
    void FrameUpdate(FrameUpdateContext& ctx);
    void FixedLogic(FixedLogicContext& ctx);

private:
    // Set by streaming on the wall clock, consumed by the next fixed tick.
    std::optional<Vec3d> PendingSafePosition;
};
