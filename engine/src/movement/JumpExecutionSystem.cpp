#include <movement/JumpExecutionSystem.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <movement/MovementTags.h>
#include <world/registry/Registry.h>

#include <cstdint>
#include <utility>

void JumpExecutionSystem::Step(World& world)
{
    if (!world.IsRegistered<GameplayTagContainer>() || !world.IsRegistered<CharacterController>()
        || !world.IsRegistered<MovementProfile>())
    {
        return;
    }
    const MovementTags* ids = std::as_const(world).TryGetResource<MovementTags>();
    if (ids == nullptr)
        return;

    if (LastWorld != &world)
    {
        CachedQuery.emplace(world);
        LastWorld = &world;
    }

    CachedQuery->ForEachChunk([&](auto& view)
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
    });
}

void JumpExecutionSystem::FixedLogic(FixedLogicContext& ctx)
{
    for (Registry* reg : ctx.ActiveRegistries)
        Step(reg->Components);
}
