#include <anim/AnimationRegistration.h>

#include <anim/AnimContentSystem.h>
#include <anim/AnimFactGatherSystem.h>
#include <anim/AnimFactProviders.h>
#include <anim/AnimSelectSystem.h>
#include <anim/AnimationClipPlaybackSystem.h>
#include <app/EngineSchedule.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/ComponentRegistrar.h>

void RegisterAnimationComponents(ComponentRegistrar& registrar)
{
    registrar.AddAll<AnimationComponents>();
}

void InstallAnimationVocabulary(World& world)
{
    if (GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>())
    {
        for (const char* layer : { "anim.layer.base", "anim.layer.upper", "anim.layer.aim" })
            (void)tags->RegisterTag(layer);
    }
    if (!world.HasResource<AnimFactProviders>())
        world.AddResource<AnimFactProviders>();
}

void RegisterAnimationSystems(EngineSchedule& schedule, LoggingProvider* logging)
{
    schedule.Register<AnimationClipPlaybackSystem>();
    schedule.Register<AnimFactGatherSystem>(logging);
    schedule.Register<AnimSelectSystem>();
    schedule.Register<AnimContentSystem>();
    // Selection reads this tick's facts; resolution reads this tick's winners.
    schedule.After<AnimSelectSystem, AnimFactGatherSystem>();
    schedule.After<AnimContentSystem, AnimSelectSystem>();
}
