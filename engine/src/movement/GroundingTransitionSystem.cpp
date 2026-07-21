#include <movement/GroundingTransitionSystem.h>

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <movement/LocomotionMode.h>
#include <movement/MovementModes.h>
#include <physics/components/CharacterController.h>

namespace
{
constexpr int kGroundingPriority = 1;

void RequestGroundingLocomotionModesImpl(
    World& world,
    const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<CharacterController>()
        || !world.IsRegistered<LocomotionModeRequest>())
    {
        return;
    }

    const ComponentTypeId onGround = ResolveComponentTypeId<OnGround>();
    const ComponentTypeId inAir = ResolveComponentTypeId<InAir>();
    Query<Read<CharacterController>, With<LocomotionModeRequest>> query(world);

    const auto visit = [&](auto& view)
    {
        const auto controllers = view.template Read<CharacterController>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            RequestLocomotionMode(
                world,
                view.Entity(i),
                controllers[i].Grounded ? onGround : inAir,
                kGroundingPriority);
        }
    };

    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);
}
} // namespace

void RequestGroundingLocomotionModes(World& world)
{
    RequestGroundingLocomotionModesImpl(world, nullptr);
}

void RequestGroundingLocomotionModes(
    World& world,
    const StoragePartitionSet& partitions)
{
    RequestGroundingLocomotionModesImpl(world, &partitions);
}

void GroundingTransitionSystem::FixedLogic(FixedLogicContext& ctx)
{
    RequestGroundingLocomotionModes(ctx.Entities, ctx.Partitions);
}

void GroundingTransitionSystem::Step(World& world)
{
    RequestGroundingLocomotionModes(world);
}
