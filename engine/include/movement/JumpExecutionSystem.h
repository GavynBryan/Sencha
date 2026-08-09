#pragma once

#include <ecs/Query.h>
#include <movement/JumpState.h>
#include <movement/MovementComponents.h>
#include <movement/MovementIntent.h>

#include <optional>

struct FixedLogicContext;
class StoragePartitionSet;
class World;

//=============================================================================
// Jump
//
// Whether a character leaves the ground this tick, and how fast.
//
// Jump lives here rather than in the ability layer because it steers the body
// the player is holding the key for. A client simulates its own movement
// immediately and replays the ticks the authority has not answered yet, so
// everything that decides where a pawn ends up has to be re-runnable from the
// state and the input of a tick alone. The ability path could not be: a press
// went through a shared queue and spawned timed effect entities, and creating
// entities is not something reconciliation is allowed to do.
//
// What that costs is nothing a player or a designer sees. The gate is the same
// (on the ground, off cooldown), the speed and the cooldown are resolved with
// the rest of movement feel, and a jump still composes with locomotion rather
// than replacing it. Actions that ask the authority for something -- fire,
// cast, interact -- still belong to AbilityKit, where waiting for an answer is
// the correct behaviour.
//=============================================================================

// Decides one character's jump for one step and advances its cooldown. Returns
// true when the jump fires, with the up-axis speed to contribute.
//
// Pure: state and input go in, a decision comes out. A replayed step and a live
// step call this with the same arguments and get the same answer.
[[nodiscard]] bool StepJump(const SupportState& support,
                            const ResolvedMovementTuning& tuning,
                            bool jumpRequested,
                            float deltaSeconds,
                            JumpState& state,
                            float& upVelocityOut);

class JumpExecutionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world, float deltaSeconds);

private:
    void Step(World& world, const StoragePartitionSet* partitions,
              float deltaSeconds);

    const World* LastWorld = nullptr;
    std::optional<Query<Write<JumpState>,
                        Read<MovementIntent>,
                        Read<SupportState>,
                        Read<ResolvedMovementTuning>>> CachedQuery;
};
