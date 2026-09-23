#include <anim/AnimationRegistration.h>

#include <anim/AnimContentSystem.h>
#include <anim/AnimEventSystem.h>
#include <anim/AnimFactGatherSystem.h>
#include <anim/AnimFactProviders.h>
#include <anim/AnimSelectSystem.h>
#include <anim/AnimationClipPlaybackSystem.h>
#include <app/EngineSchedule.h>
#include <core/console/ConsoleRegistry.h>
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

void RegisterAnimationSystems(EngineSchedule& schedule, LoggingProvider* logging, const AnimEventHost& events)
{
    schedule.Register<AnimationClipPlaybackSystem>();
    schedule.Register<AnimFactGatherSystem>(logging);
    schedule.Register<AnimSelectSystem>();
    schedule.Register<AnimContentSystem>();
    AnimEventSystem& eventSystem = schedule.Register<AnimEventSystem>(events.Verbs, events.PresentsPose);
    // Selection reads this tick's facts; resolution reads this tick's winners;
    // events cross the content time resolution just advanced.
    schedule.After<AnimSelectSystem, AnimFactGatherSystem>();
    schedule.After<AnimContentSystem, AnimSelectSystem>();
    schedule.After<AnimEventSystem, AnimContentSystem>();

    if (events.Console != nullptr)
    {
        (void)events.Console->RegisterCVar({
            .Name = "anim.events.queue_capacity",
            .Owner = "engine",
            .Type = CVarType::Int,
            .DefaultValue = static_cast<std::int64_t>(eventSystem.GetCapacity()),
            .CurrentValue = static_cast<std::int64_t>(eventSystem.GetCapacity()),
            .Flags = CVarFlags::None,
            .Help = "How many clip events one tick may offer to their bindings. A crossing past it is "
                    "recorded as refused rather than dropped unseen.",
            .Source = { "engine" },
            .Min = 1.0,
            .Max = 65536.0,
            .OnChange = [&eventSystem](const CVarChangeContext& ctx) {
                eventSystem.SetCapacity(static_cast<std::size_t>(std::get<std::int64_t>(ctx.NewValue)));
            },
        });
    }
}
