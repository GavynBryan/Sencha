#include <movement/DilatedCharacterStepSystem.h>

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/CharacterTickStep.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/components/MotionChannels.h>
#include <physics/PhysicsStepSystem.h>
#include <world/SimulationTimeline.h>

DilatedCharacterStepSystem::DilatedCharacterStepSystem(const FreeLocomotionSystem& locomotion,
                                                       PhysicsStepSystem* physics)
    : Locomotion(&locomotion)
    , Physics(physics)
{
}

void DilatedCharacterStepSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<ActorTimeDilated>() || !world.IsRegistered<MovementIntent>()
        || !world.IsRegistered<CharacterMovement>())
        return;
    if (!Dilated)
        Dilated.emplace(world);

    // Entities first: a step sweeps through physics and may change what the query
    // is walking.
    Pending.clear();
    Dilated->ForEachChunkIn(ctx.Partitions, [&](auto& view) {
        const auto intents = view.template Read<MovementIntent>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            Pending.emplace_back(view.Entity(i), intents[i]);
    });
    CharacterMoverPool* movers = Physics != nullptr ? &Physics->GetCharacterMovers() : nullptr;
    for (const auto& [entity, intent] : Pending)
    {
        const ActorTime* time = world.TryGet<ActorTime>(entity);
        if (time == nullptr || time->ScaleQ16 <= 0 || !CharacterTickModeSupported(world, entity))
            continue;
        MotionImpulse impulse;
        if (MotionImpulse* pending = world.TryGet<MotionImpulse>(entity))
        {
            impulse = *pending;
            *pending = {};
        }
        StepScaledCharacterTick(world, movers, entity, intent, SimulationTickOf(world, entity, ctx.Time.TickIndex),
                                static_cast<float>(ctx.Time.DeltaSeconds), time->ScaleQ16, Locomotion->GetGravity(),
                                Locomotion->GetUpAxis(), &impulse);
    }
}
