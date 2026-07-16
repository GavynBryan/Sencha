#include <effects/AttributeResolveSystem.h>

#include <app/GameContexts.h>
#include <attributes/AttributeRegistry.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <world/registry/Registry.h>

void AttributeResolveSystem::FixedLogic(FixedLogicContext& ctx)
{
    Registry& global = *ctx.Registries.Global;
    const auto& effects = global.Resources.Get<EffectRegistry>();
    const auto& attributes = global.Resources.Get<AttributeRegistry>();

    for (Registry* reg : ctx.ActiveRegistries)
        ResolveAttributesWithEffects(reg->Entities, effects, attributes);
}
