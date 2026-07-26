#include <abilities/AbilityActivationSystem.h>

#include <abilities/AbilitySystem.h>
#include <app/GameContexts.h>

void AbilityActivationSystem::FixedLogic(FixedLogicContext& ctx)
{
    ProcessAbilityActivations(ctx.Entities, ctx.Partitions);
}
