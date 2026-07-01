#include <effects/EffectLifetimeSystem.h>

#include <app/GameContexts.h>
#include <effects/EffectSystem.h>
#include <world/registry/Registry.h>

void EffectLifetimeSystem::FixedLogic(FixedLogicContext& ctx)
{
    const float dt = static_cast<float>(ctx.Time.DeltaSeconds);
    for (Registry* reg : ctx.ActiveRegistries)
        TickEffects(reg->Components, dt);
}
