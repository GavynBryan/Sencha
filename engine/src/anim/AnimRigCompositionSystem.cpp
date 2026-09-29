#include <anim/AnimRigCompositionSystem.h>

#include <anim/AnimFacts.h>
#include <anim/AnimFlowState.h>
#include <anim/AnimPoseState.h>
#include <anim/AnimSelectorState.h>
#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/CommandBuffer.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <algorithm>

namespace
{
    constexpr AnimRigParts Part(AnimRigPart part) { return static_cast<AnimRigParts>(part); }

    template <typename Component>
    AnimRigParts PartIfCarried(const World& world, EntityId entity, AnimRigPart part)
    {
        return world.IsRegistered<Component>() && world.HasComponent<Component>(entity) ? Part(part) : 0;
    }

    // What an entity carries before it was ever composed, from whatever put it there.
    AnimRigParts CarriedParts(const World& world, EntityId entity)
    {
        return PartIfCarried<AnimFacts>(world, entity, AnimRigPart::Facts)
             | PartIfCarried<AnimFactsLarge>(world, entity, AnimRigPart::FactsLarge)
             | PartIfCarried<AnimFactHistory>(world, entity, AnimRigPart::History)
             | PartIfCarried<AnimSelectorState>(world, entity, AnimRigPart::Selection)
             | PartIfCarried<AnimFlowState>(world, entity, AnimRigPart::Flows)
             | PartIfCarried<AnimPoseState>(world, entity, AnimRigPart::Pose);
    }

    template <typename Component>
    void Compose(EntityId entity, AnimRigParts from, AnimRigParts to, AnimRigPart part, CommandBuffer& commands)
    {
        const bool had = HasAnimRigPart(from, part);
        const bool needs = HasAnimRigPart(to, part);
        if (needs && !had)
            commands.AddComponent(entity, Component{});
        else if (had && !needs)
            commands.RemoveComponent<Component>(entity);
    }
}

AnimRigParts AnimRigRequiredParts(const AnimBoundRig& rig, bool presentsPose, bool consumesPose)
{
    AnimRigParts parts = 0;
    if (!ShouldRunAnimationLogic(presentsPose, rig))
        return parts;
    if (rig.HasFacts)
        parts |= Part(rig.Capacity == AnimFactCapacity::Large ? AnimRigPart::FactsLarge : AnimRigPart::Facts);
    if (rig.DerivationsKeepMemory)
        parts |= Part(AnimRigPart::History);
    if (std::ranges::any_of(rig.Layers, [](const AnimBoundLayer& layer) { return layer.Selector >= 0; }))
        parts |= Part(AnimRigPart::Selection);
    if (!rig.Flows.empty())
        parts |= Part(AnimRigPart::Flows);
    if (presentsPose && consumesPose && rig.Skeleton.IsValid())
        parts |= Part(AnimRigPart::Pose);
    return parts;
}

void ComposeAnimRigParts(EntityId entity, AnimRigParts from, AnimRigParts to, CommandBuffer& commands)
{
    Compose<AnimFacts>(entity, from, to, AnimRigPart::Facts, commands);
    Compose<AnimFactsLarge>(entity, from, to, AnimRigPart::FactsLarge, commands);
    Compose<AnimFactHistory>(entity, from, to, AnimRigPart::History, commands);
    Compose<AnimSelectorState>(entity, from, to, AnimRigPart::Selection, commands);
    Compose<AnimFlowState>(entity, from, to, AnimRigPart::Flows, commands);
    Compose<AnimPoseState>(entity, from, to, AnimRigPart::Pose, commands);
}

AnimRigCompositionSystem::AnimRigCompositionSystem(bool presentsPose, LoggingProvider* logging)
    : PresentsPose(presentsPose)
    , Logging(logging)
{
}

void AnimRigCompositionSystem::Report(const AnimBoundRig& rig)
{
    if (Logging == nullptr || rig.Diagnostics.empty() || !Reported.insert(rig.Generation).second)
        return;
    auto& logger = Logging->GetLogger<AnimRigCompositionSystem>();
    for (const AnimDiagnostic& diagnostic : rig.Diagnostics)
    {
        if (diagnostic.Severity == AnimDiagnosticSeverity::Error)
            logger.Error(FormatAnimDiagnostic(diagnostic));
        else
            logger.Warn(FormatAnimDiagnostic(diagnostic));
    }
}

void AnimRigCompositionSystem::FixedLogic(FixedLogicContext& ctx)
{
    ComposeImpl(ctx.Entities, &ctx.Partitions);
}

void AnimRigCompositionSystem::Compose(World& world)
{
    ComposeImpl(world, nullptr);
}

void AnimRigCompositionSystem::ComposeImpl(World& world, const StoragePartitionSet* partitions)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRigComposition>())
        return;
    if (LastWorld != &world)
    {
        Consumed.reset();
        Unconsumed.reset();
        Orphans.reset();
        LastWorld = &world;
    }
    if (!Consumed)
        Consumed.emplace(world);
    if (!Unconsumed)
        Unconsumed.emplace(world);
    if (!Orphans)
        Orphans.emplace(world);

    const auto each = [&](auto& query, auto&& fn) {
        if (partitions != nullptr)
            query.ForEachChunkIn(*partitions, fn);
        else
            query.ForEachChunk(fn);
    };

    CommandBuffer commands(world);
    AnimRigRunCache rigs(*bindings, world);
    const auto compose = [&](auto& view, bool consumesPose) {
        const auto handles = view.template Read<AnimRig>();
        auto compositions = view.template Write<AnimRigComposition>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = rigs.Resolve(handles[i].Rig);
            if (rig != nullptr)
                Report(*rig);
            // An unbound or invalid rig says nothing about what it needs, so what the
            // entity carries waits for one that does.
            if (rig == nullptr || !rig->Valid)
                continue;
            AnimRigComposition& composition = compositions[i];
            const AnimRigParts parts = AnimRigRequiredParts(*rig, PresentsPose, consumesPose);
            if (composition.Composed && composition.Parts == parts)
                continue;
            const AnimRigParts from = composition.Composed ? composition.Parts : CarriedParts(world, view.Entity(i));
            ComposeAnimRigParts(view.Entity(i), from, parts, commands);
            composition.Parts = parts;
            composition.Composed = true;
        }
    };
    each(*Consumed, [&](auto& view) { compose(view, true); });
    each(*Unconsumed, [&](auto& view) { compose(view, false); });
    each(*Orphans, [&](auto& view) {
        const auto compositions = view.template Read<AnimRigComposition>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            ComposeAnimRigParts(view.Entity(i), compositions[i].Parts, 0, commands);
            commands.RemoveComponent<AnimRigComposition>(view.Entity(i));
        }
    });
    commands.Flush();
}
