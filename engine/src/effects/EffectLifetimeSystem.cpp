#include <effects/EffectLifetimeSystem.h>

#include <app/GameContexts.h>
#include <effects/EffectSystem.h>

void EffectLifetimeSystem::FixedLogic(FixedLogicContext& ctx)
{
    TickEffects(
        ctx.Entities,
        static_cast<float>(ctx.Time.DeltaSeconds),
        ctx.Partitions);
}
