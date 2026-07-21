#pragma once

#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <movement/MovementProfile.h>
#include <physics/components/CharacterController.h>

#include <optional>

struct FixedLogicContext;
class StoragePartitionSet;
class World;

class JumpExecutionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Step(World& world);

private:
    void Step(World& world, const StoragePartitionSet* partitions);

    const World* LastWorld = nullptr;
    std::optional<Query<Write<GameplayTagContainer>,
                        Write<CharacterController>,
                        Read<MovementProfile>>> CachedQuery;
};
