#include <anim/AnimationClipPlaybackSystem.h>

#include <anim/AnimationClipPlaybackRuntime.h>
#include <anim/AnimationClipPlayerComponent.h>
#include <app/EngineSchedule.h>
#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <cmath>

void AdvanceAnimationClipPlayers(World& world,
                                 const StoragePartitionSet& partitions,
                                 const AnimationClipCache& clips,
                                 float deltaSeconds)
{
    if (!world.IsRegistered<AnimationClipPlayerComponent>())
        return;

    Query<Write<AnimationClipPlayerComponent>> query(world);
    query.ForEachChunkIn(partitions, [&](auto& view)
    {
        auto players = view.template Write<AnimationClipPlayerComponent>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            AnimationClipPlayerComponent& player = players[i];
            if (player.Rate == 0.0f)
                continue;
            const AnimationClipData* clip = clips.Get(player.Clip);
            if (clip == nullptr || clip->DurationSeconds <= 0.0f)
                continue;

            player.TimeSeconds += deltaSeconds * player.Rate;
            if (player.Loop)
            {
                // fmod keeps a long pause or a high rate from walking the
                // time forward one duration at a time; the sign fixup makes
                // reverse playback wrap rather than run negative.
                player.TimeSeconds =
                    std::fmod(player.TimeSeconds, clip->DurationSeconds);
                if (player.TimeSeconds < 0.0f)
                    player.TimeSeconds += clip->DurationSeconds;
            }
            else
            {
                player.TimeSeconds = std::clamp(
                    player.TimeSeconds, 0.0f, clip->DurationSeconds);
            }
        }
    });
}

void RegisterAnimationSystems(EngineSchedule& schedule)
{
    schedule.Register<AnimationClipPlaybackSystem>();
}

void AnimationClipPlaybackSystem::FixedLogic(FixedLogicContext& ctx)
{
    const auto* runtime = ctx.Entities.TryGetResource<AnimationClipPlaybackRuntime>();
    if (runtime == nullptr || runtime->Clips == nullptr)
        return;
    AdvanceAnimationClipPlayers(ctx.Entities, ctx.Partitions, *runtime->Clips,
                                static_cast<float>(ctx.Time.DeltaSeconds));
}
