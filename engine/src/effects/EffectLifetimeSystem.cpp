#include <effects/EffectLifetimeSystem.h>

#include <app/GameContexts.h>
#include <attributes/AttributeRegistry.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <world/registry/Registry.h>

void EffectLifetimeSystem::FixedLogic(FixedLogicContext& ctx)
{
    const float dt = static_cast<float>(ctx.Time.DeltaSeconds);
    Registry& global = *ctx.Registries.Global;
    const auto& effects = global.Resources.Get<EffectRegistry>();
    const auto& attributes = global.Resources.Get<AttributeRegistry>();

    for (Registry* reg : ctx.ActiveRegistries)
        TickEffects(reg->Components, dt, effects, attributes);
}
