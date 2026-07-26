#include <effects/AttributeResolveSystem.h>

#include <app/GameContexts.h>
#include <effects/EffectSystem.h>

void AttributeResolveSystem::FixedLogic(FixedLogicContext& ctx)
{
    ResolveAttributesWithEffects(ctx.Entities, ctx.Partitions);
}
