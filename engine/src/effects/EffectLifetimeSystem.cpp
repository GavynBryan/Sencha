#include <effects/EffectLifetimeSystem.h>

#include <app/GameContexts.h>
#include <effects/AppliedEffects.h>
#include <effects/EffectSystem.h>

void EffectLifetimeSystem::FixedLogic(FixedLogicContext& ctx)
{
    RestoreAppliedEffects(ctx.Entities, ctx.Time, ctx.Partitions);
    TickEffects(ctx.Entities, ctx.Partitions);
}
