#include <movement/JumpExecutionSystem.h>

#include <app/GameContexts.h>
#include <ecs/World.h>
#include <movement/MovementTags.h>
#include <world/registry/Registry.h>

#include <cstdint>
#include <utility>

void JumpExecutionSystem::Step(World& world, const MovementTags& movementTags)
{
    if (!world.IsRegistered<GameplayTagContainer>() || !world.IsRegistered<CharacterController>()
        || !world.IsRegistered<MovementProfile>())
    {
        return;
    }

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
            if (!tags[i].HasExact(movementTags.JumpRequested))
                continue;
            controllers[i].PendingJumpSpeed = profiles[i].JumpSpeed;
            tags[i].Revoke(movementTags.JumpRequested);
        }
    });
}

void JumpExecutionSystem::FixedLogic(FixedLogicContext& ctx)
{
    const MovementTags& tags = ctx.Registries.Global->Resources.Get<MovementTags>();
    for (Registry* reg : ctx.ActiveRegistries)
        Step(reg->Components, tags);
}
