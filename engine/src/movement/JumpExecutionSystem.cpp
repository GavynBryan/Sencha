#include <movement/JumpExecutionSystem.h>

#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <movement/MovementTags.h>

#include <cstdint>
#include <utility>

void JumpExecutionSystem::Step(World& world)
{
    Step(world, nullptr);
}

void JumpExecutionSystem::Step(
    World& world,
    const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<GameplayTagContainer>()
        || !world.IsRegistered<CharacterController>()
        || !world.IsRegistered<MovementProfile>())
    {
        return;
    }
    const MovementTags* ids =
        std::as_const(world).TryGetResource<MovementTags>();
    if (ids == nullptr)
        return;

    if (LastWorld != &world)
    {
        CachedQuery.emplace(world);
        LastWorld = &world;
    }

    const auto visit = [&](auto& view)
    {
        auto tags = view.template Write<GameplayTagContainer>();
        auto controllers = view.template Write<CharacterController>();
        const auto profiles = view.template Read<MovementProfile>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            if (!tags[i].HasExact(ids->JumpRequested))
                continue;
            controllers[i].PendingJumpSpeed = profiles[i].JumpSpeed;
            tags[i].Revoke(ids->JumpRequested);
        }
    };

    if (partitions != nullptr)
        CachedQuery->ForEachChunkIn(*partitions, visit);
    else
        CachedQuery->ForEachChunk(visit);
}

void JumpExecutionSystem::FixedLogic(FixedLogicContext& ctx)
{
    Step(ctx.Entities, &ctx.Partitions);
}
