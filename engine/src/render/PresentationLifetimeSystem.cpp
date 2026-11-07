#include <render/PresentationLifetimeSystem.h>

#include <app/GameContexts.h>
#include <render/MeshOverrides.h>
#include <render/outline/OutlineContributions.h>
#include <render/post/PostEffectContributions.h>

void PresentationLifetimeSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    SweepMeshOverrideHolders(ctx.Entities);
    SweepOutlines(ctx.Entities);
    AdvancePostEffectLifetimes(ctx.Entities, static_cast<float>(ctx.WallDeltaSeconds));
}
