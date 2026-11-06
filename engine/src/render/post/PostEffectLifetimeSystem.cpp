#include <render/post/PostEffectLifetimeSystem.h>

#include <app/GameContexts.h>
#include <render/post/PostEffectContributions.h>

void PostEffectLifetimeSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    AdvancePostEffectLifetimes(ctx.Entities, static_cast<float>(ctx.WallDeltaSeconds));
}
