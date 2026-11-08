#include <logic/EffectReactionSystem.h>

#include <app/GameContexts.h>
#include <assets/data/DataAssetCache.h>
#include <attributes/AttributeRegistry.h>
#include <camera/CameraShake.h>
#include <camera/CameraShakeProfile.h>
#include <core/logging/Logger.h>
#include <ecs/World.h>
#include <effects/EffectApplicationLog.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <logic/EffectReactions.h>
#include <movement/MotionComposition.h>
#include <movement/components/MotionChannels.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <utility>

EffectReactionSystem::EffectReactionSystem(Services services)
    : Use(services)
{
}

void EffectReactionSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    EffectApplicationLog* log = world.TryGetResource<EffectApplicationLog>();
    if (log == nullptr)
    {
        (void)world.AddResource<EffectApplicationLog>();
        return;
    }
    if (BoundWorld != &world)
    {
        Profiles.clear();
        BoundWorld = &world;
    }
    if (IsSimulationAuthority(world) && world.IsRegistered<EffectReactions>())
        React(world, ctx);
    log->Clear();
}

void EffectReactionSystem::React(World& world, const FixedLogicContext& ctx)
{
    const EffectApplicationLog& log = world.GetResource<EffectApplicationLog>();
    const EffectRegistry* effects = world.TryGetResource<EffectRegistry>();
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    if (effects == nullptr || tags == nullptr || Use.DataAssets == nullptr)
        return;
    const World& view = world;
    for (const EffectApplication& application : log.Applications())
    {
        const EffectReactions* reactions =
            world.IsAlive(application.Target) ? view.TryGet<EffectReactions>(application.Target) : nullptr;
        const WorldTransform* pose = reactions != nullptr ? view.TryGet<WorldTransform>(application.Target) : nullptr;
        const EffectDefinition* definition = effects->Get(application.Effect);
        if (pose == nullptr || definition == nullptr)
            continue;
        const std::vector<EffectReactionRule>* rules = RulesFor(world, reactions->Profile);
        if (rules == nullptr)
            continue;

        EffectReactionCase reaction{ .Kinds = &definition->Kinds,
                                     .Recipient = world.IsRegistered<GameplayTagContainer>()
                                                      ? view.TryGet<GameplayTagContainer>(application.Target)
                                                      : nullptr,
                                     .Changes = log.ChangesOf(application),
                                     .Source = application.Source,
                                     .TargetPosition = pose->Value.Position,
                                     .InstigatorPosition = std::nullopt };
        if (world.IsAlive(application.Source.Instigator))
            if (const WorldTransform* instigator = view.TryGet<WorldTransform>(application.Source.Instigator))
                reaction.InstigatorPosition = instigator->Value.Position;
        if (world.IsRegistered<LocomotionOutput>())
            if (const LocomotionOutput* locomotion = view.TryGet<LocomotionOutput>(application.Target))
                reaction.Up = locomotion->UpAxis;

        const std::optional<EffectReactionOutcome> outcome = ReactToEffect(*rules, reaction, *tags);
        if (!outcome)
            continue;
        if (outcome->Impulse.Magnitude() > 0.0f)
            (void)AddMotionImpulse(world, application.Target, outcome->Impulse);
        CameraShakeState* shakes = world.TryGetResource<CameraShakeState>();
        const EffectReactionRule& rule = (*rules)[outcome->Rule];
        if (shakes == nullptr || !(outcome->ShakeScale > 0.0f))
            continue;
        const CameraShakeProfile* profile = Use.DataAssets->TryGet<CameraShakeProfile>(
            Use.DataAssets->Find(rule.ShakeProfile), kCameraShakeType);
        if (profile == nullptr)
            continue;
        const std::uint64_t tick = ctx.Time.TickIndex;
        shakes->Start(CameraShakeInstance{ .Timing = CameraEffectTiming::Once(profile->Envelope, tick, application.Target),
                                           .Profile = *profile,
                                           .Scale = outcome->ShakeScale,
                                           .Seed = tick ^ application.Target.Index,
                                           .Point = std::nullopt },
                      { .Tick = tick + 1, .Alpha = 0.0, .TickSeconds = ctx.Time.DeltaSeconds });
    }
}

const std::vector<EffectReactionRule>* EffectReactionSystem::RulesFor(const World& world, DataAssetHandle profile)
{
    const EffectReactionProfile* authored =
        Use.DataAssets->TryGet<EffectReactionProfile>(profile, kEffectReactionProfileType);
    const AttributeRegistry* attributes = world.TryGetResource<AttributeRegistry>();
    const GameplayTagRegistry* tags = world.TryGetResource<GameplayTagRegistry>();
    if (authored == nullptr || attributes == nullptr || tags == nullptr)
        return nullptr;
    const std::uint64_t version = Use.DataAssets->GetReloadVersion(profile);
    auto bound = std::ranges::find(Profiles, profile, &BoundProfile::Profile);
    if (bound != Profiles.end() && bound->Version == version)
        return &bound->Rules;
    if (bound == Profiles.end())
        bound = Profiles.insert(Profiles.end(), BoundProfile{ .Profile = profile, .Version = 0, .Rules = {} });
    bound->Version = version;
    std::vector<std::string> errors;
    if (!ResolveEffectReactionProfile(*authored, *attributes, *tags, bound->Rules, errors) && Use.Log != nullptr)
        for (const std::string& error : errors)
            Use.Log->Error("Effect reactions '{}': {}", Use.DataAssets->GetName(profile), error);
    return &bound->Rules;
}
