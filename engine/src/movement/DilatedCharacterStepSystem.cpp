#include <movement/DilatedCharacterStepSystem.h>

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/CharacterTickStep.h>
#include <movement/FreeLocomotionSystem.h>
#include <physics/PhysicsStepSystem.h>
#include <world/SimulationTimeline.h>

#include <utility>

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
        if (time == nullptr)
            continue;
        // This step is the one composition left this character to, so it takes
        // the mailboxes' contents and empties them, as composition would have.
        CharacterTickContributions contributions;
        if (MotionAxisOverride* overrides = world.TryGet<MotionAxisOverride>(entity))
            contributions.Overrides = std::exchange(*overrides, MotionAxisOverride{});
        if (MotionImpulse* impulse = world.TryGet<MotionImpulse>(entity))
            contributions.Impulse = std::exchange(*impulse, MotionImpulse{});
        StepScaledCharacterTick(world, movers, entity, intent, SimulationTickOf(world, entity, ctx.Time.TickIndex),
                                static_cast<float>(ctx.Time.DeltaSeconds), time->ScaleQ16, Locomotion->GetGravity(),
                                Locomotion->GetUpAxis(), &contributions);
    }
}
