#include <abilities/AbilityActivationSystem.h>

#include <app/GameContexts.h>
#include <abilities/AbilitySystem.h>
#include <world/registry/Registry.h>

void AbilityActivationSystem::FixedLogic(FixedLogicContext& ctx)
{
    for (Registry* reg : ctx.ActiveRegistries)
        ProcessAbilityActivations(reg->Components);
}
