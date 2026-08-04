#pragma once

#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementComponents.h>

#include <optional>

struct FixedLogicContext;
class StoragePartitionSet;
class World;

//=============================================================================
// JumpExecutionSystem
//
// Consumes the one-tick movement.jump.requested tag the Jump ability grants
// and turns it into an up-axis motion contribution at this tick's resolved
// jump speed. It writes a contribution rather than a velocity so a jump
// composes with whatever locomotion and other actions produced, including the
// support velocity a moving platform contributes.
//=============================================================================
class JumpExecutionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);

private:
    void Step(World& world, const StoragePartitionSet* partitions);

    const World* LastWorld = nullptr;
    std::optional<Query<Write<GameplayTagContainer>,
                        Read<ResolvedMovementTuning>>> CachedQuery;
};
