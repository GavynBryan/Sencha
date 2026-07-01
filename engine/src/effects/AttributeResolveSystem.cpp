#include <effects/AttributeResolveSystem.h>

#include <app/GameContexts.h>
#include <effects/EffectSystem.h>
#include <world/registry/Registry.h>

void AttributeResolveSystem::FixedLogic(FixedLogicContext& ctx)
{
    for (Registry* reg : ctx.ActiveRegistries)
        ResolveAttributesWithEffects(reg->Components);
}
