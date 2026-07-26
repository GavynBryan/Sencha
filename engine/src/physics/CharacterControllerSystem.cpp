#include <physics/CharacterControllerSystem.h>

#include <app/GameContexts.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsStepSystem.h>
#include <physics/components/CharacterController.h>

CharacterControllerSystem::CharacterControllerSystem(
    PhysicsStepSystem& step)
    : Step(&step)
{
}

void CharacterControllerSystem::Physics(PhysicsContext& ctx)
{
    if (!ctx.Entities.IsRegistered<CharacterController>())
        return;

    CharacterMoverPool& movers = Step->GetCharacterMovers();
    movers.Reconcile(ctx.Entities, ctx.Partitions);
    movers.Drive(
        ctx.Entities,
        ctx.Partitions,
        static_cast<float>(ctx.Time.DeltaSeconds),
        Gravity);
}
