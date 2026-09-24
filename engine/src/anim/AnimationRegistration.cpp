#include <anim/AnimationRegistration.h>

#include <anim/AnimContentSystem.h>
#include <anim/AnimEventSystem.h>
#include <anim/AnimFactGatherSystem.h>
#include <anim/AnimFactProviders.h>
#include <anim/AnimPoseSystem.h>
#include <anim/AnimRequestJournal.h>
#include <anim/AnimRigBinding.h>
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
    if (!world.HasResource<AnimRigLimits>())
        world.AddResource<AnimRigLimits>();
    if (!world.HasResource<AnimRequestJournal>())
        world.AddResource<AnimRequestJournal>();
}

void RegisterAnimationCVars(ConsoleRegistry& console, World& world)
{
    const AnimRigLimits* limits = world.TryGetResource<AnimRigLimits>();
    const std::int64_t cap = limits != nullptr ? limits->BlendOverrideCap : AnimRigLimits{}.BlendOverrideCap;
    (void)console.RegisterCVar({
        .Name = "anim.blend.override_cap",
        .Owner = "engine",
        .Type = CVarType::Int,
        .DefaultValue = cap,
        .CurrentValue = cap,
        .Flags = CVarFlags::None,
        .Help = "How many pairwise blend overrides one rig may bind. Rigs past half of it warn; past it "
                "they fail to bind. Changing it rebinds every rig.",
        .Source = { "engine" },
        .Min = 0.0,
        .Max = 4096.0,
        .OnChange = [&world](const CVarChangeContext& ctx) {
            if (AnimRigLimits* live = world.TryGetResource<AnimRigLimits>())
                live->BlendOverrideCap = static_cast<std::uint32_t>(std::get<std::int64_t>(ctx.NewValue));
        },
    });
}

void RegisterAnimationSystems(EngineSchedule& schedule, LoggingProvider* logging, const AnimationHost& host)
{
    schedule.Register<AnimationClipPlaybackSystem>();
    schedule.Register<AnimFactGatherSystem>(logging);
    schedule.Register<AnimSelectSystem>();
    schedule.Register<AnimContentSystem>();
    AnimEventSystem& eventSystem = schedule.Register<AnimEventSystem>(host.Verbs, host.PresentsPose);
    // Selection reads this tick's facts; resolution reads this tick's winners;
    // events cross the content time resolution just advanced.
    schedule.After<AnimSelectSystem, AnimFactGatherSystem>();
    schedule.After<AnimContentSystem, AnimSelectSystem>();
    schedule.After<AnimEventSystem, AnimContentSystem>();

    // Posing runs in the post-fixed phase, after movement has moved what it
    // poses; nothing in the fixed phase reads it.
    if (host.PresentsPose)
        schedule.Register<AnimPoseSystem>(host.Jobs);

    if (host.Console != nullptr)
    {
        (void)host.Console->RegisterCVar({
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
