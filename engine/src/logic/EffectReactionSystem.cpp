#include <logic/EffectReactionSystem.h>

#include <anim/AnimRequestJournal.h>
#include <anim/AnimRequests.h>
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

namespace
{
    EffectReactionCase CaseOf(const World& world, const EffectApplicationLog& log, const EffectApplication& application,
                              const EffectDefinition& definition, const WorldTransform& pose)
    {
        EffectReactionCase reaction{ .Kinds = &definition.Kinds,
                                     .Recipient = world.IsRegistered<GameplayTagContainer>()
                                                      ? world.TryGet<GameplayTagContainer>(application.Target)
                                                      : nullptr,
                                     .Changes = log.ChangesOf(application),
                                     .Source = application.Source,
                                     .TargetPosition = pose.Value.Position,
                                     .InstigatorPosition = std::nullopt,
                                     .Forward = pose.Value.Rotation.RotateVector(Vec3d::Forward()) };
        if (world.IsAlive(application.Source.Instigator))
            if (const WorldTransform* instigator = world.TryGet<WorldTransform>(application.Source.Instigator))
                reaction.InstigatorPosition = instigator->Value.Position;
        if (world.IsRegistered<LocomotionOutput>())
            if (const LocomotionOutput* locomotion = world.TryGet<LocomotionOutput>(application.Target))
                reaction.Up = locomotion->UpAxis;
        return reaction;
    }

    // An entity with no rig, or a rig that plays no such intent, turns the request down.
    void Flinch(World& world, EntityId target, const EffectReactionOutcome& outcome, std::uint64_t tick)
    {
        if (!outcome.AnimationIntent.IsValid())
            return;
        AnimRequestDesc flinch;
        flinch.Source = target;
        flinch.Intent = outcome.AnimationIntent;
        flinch.Lifetime = AnimRequestLifetime::Fixed;
        flinch.FixedTicks = outcome.AnimationTicks;
        (void)RequestAnimation(world, target, flinch, tick);
    }

    void Shake(World& world, const DataAssetCache& assets, EntityId target, const EffectReactionRule& rule,
               float scale, const FixedSimTime& time)
    {
        CameraShakeState* shakes = world.TryGetResource<CameraShakeState>();
        if (shakes == nullptr || !(scale > 0.0f))
            return;
        const CameraShakeProfile* profile =
            assets.TryGet<CameraShakeProfile>(assets.Find(rule.ShakeProfile), kCameraShakeType);
        if (profile == nullptr)
            return;
        const std::uint64_t tick = time.TickIndex;
        shakes->Start(CameraShakeInstance{ .Timing = CameraEffectTiming::Once(profile->Envelope, tick, target),
                                           .Profile = *profile,
                                           .Scale = scale,
                                           .Seed = tick ^ target.Index,
                                           .Point = std::nullopt },
                      { .Tick = tick + 1, .Alpha = 0.0, .TickSeconds = time.DeltaSeconds });
    }
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
        const std::vector<EffectReactionRule>* rules =
            pose != nullptr && definition != nullptr ? RulesFor(world, reactions->Profile) : nullptr;
        if (rules == nullptr)
            continue;
        const std::optional<EffectReactionOutcome> outcome =
            ReactToEffect(*rules, CaseOf(view, log, application, *definition, *pose), *tags);
        if (!outcome)
            continue;
        if (outcome->Impulse.Magnitude() > 0.0f)
            (void)AddMotionImpulse(world, application.Target, outcome->Impulse);
        Flinch(world, application.Target, *outcome, ctx.Time.TickIndex);
        Shake(world, *Use.DataAssets, application.Target, (*rules)[outcome->Rule], outcome->ShakeScale, ctx.Time);
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
    auto bound = std::ranges::find(Profiles, profile, [](const BoundProfile& held) { return held.Asset.Asset; });
    if (bound != Profiles.end() && bound->Asset.Matches(*Use.DataAssets) && bound->Tags.Matches(*tags))
        return &bound->Rules;
    if (bound == Profiles.end())
        bound = Profiles.insert(Profiles.end(), BoundProfile{});
    bound->Asset = DataAssetStamp::Of(*Use.DataAssets, profile);
    bound->Tags = TagVocabularyStamp::Of(*tags);
    std::vector<std::string> errors;
    if (!ResolveEffectReactionProfile(*authored, *attributes, *tags, bound->Rules, errors) && Use.Log != nullptr)
        for (const std::string& error : errors)
            Use.Log->Error("Effect reactions '{}': {}", Use.DataAssets->GetName(profile), error);
    return &bound->Rules;
}
