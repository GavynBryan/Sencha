#include <abilities/AbilityActivationSystem.h>

#include <abilities/AbilityActivation.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySystem.h>
#include <app/GameContexts.h>
#include <attributes/AttributeRegistry.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/registry/Registry.h>

void AbilityActivationSystem::FixedLogic(FixedLogicContext& ctx)
{
    // Session definitions live once in the global registry; the per-registry
    // activation queue is registry-local state.
    Registry& global = *ctx.Registries.Global;
    const auto& abilities = global.Resources.Get<AbilityRegistry>();
    const auto& tags = global.Resources.Get<GameplayTagRegistry>();
    const auto& effects = global.Resources.Get<EffectRegistry>();
    const auto& attributes = global.Resources.Get<AttributeRegistry>();

    for (Registry* reg : ctx.ActiveRegistries)
    {
        AbilityActivationQueue* queue = reg->Resources.TryGet<AbilityActivationQueue>();
        if (queue == nullptr)
            continue;
        ProcessAbilityActivations(reg->Entities, *queue, abilities, tags, effects, attributes);
    }
}
