#include <anim/AnimContentSystem.h>

#include <anim/AnimFacts.h>
#include <anim/AnimRequests.h>
#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <algorithm>
#include <cmath>

namespace
{
    AnimBehaviorKind KindOf(const AnimBoundRig& rig, GameplayTagId behavior)
    {
        const AnimBoundBehavior* bound = rig.FindBehavior(behavior);
        return bound != nullptr ? bound->Policy.Kind : AnimBehaviorKind::Cyclic;
    }

    float DurationOf(const AnimBoundRig& rig, std::uint16_t content)
    {
        return content < rig.Contents.size() ? rig.Contents[content].DurationSeconds : 0.0f;
    }

    void Log(AnimDecisionLog* log, AnimTick now, std::size_t layer, AnimDecisionCause cause,
             AnimChangeReason reason, const AnimLayerContent& state)
    {
        if (log == nullptr)
            return;
        AnimDecisionRecord record;
        record.Tick = now;
        record.Layer = static_cast<std::uint8_t>(layer);
        record.Cause = cause;
        record.Reason = reason;
        record.Behavior = state.Behavior;
        record.Row = state.Row;
        record.Content = state.Content;
        log->Append(record);
    }
}

GameplayTagId AnimLayerBehavior(const AnimBoundRig& rig, std::size_t layer, const AnimSelectorState* selection,
                                const AnimRequestSet* requests, AnimTick now)
{
    const AnimBoundLayer& bound = rig.Layers[layer];
    if (bound.Selector >= 0)
    {
        if (selection != nullptr && selection->Layers[layer].Winner != kAnimNoRule)
            return selection->Layers[layer].Behavior;
        return bound.Idle;
    }
    const AnimRequest* newest = NewestAnimLayerRequest(requests, layer, now);
    return newest != nullptr ? newest->Intent : bound.Idle;
}

const AnimRequest* NewestAnimLayerRequest(const AnimRequestSet* requests, std::size_t layer, AnimTick now)
{
    if (requests == nullptr)
        return nullptr;
    const AnimRequest* newest = nullptr;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << layer);
    for (const AnimRequest& request : requests->Records)
    {
        if ((request.Layers & bit) == 0 || !IsAnimRequestLive(request, now))
            continue;
        if (newest == nullptr || request.StartTick > newest->StartTick
            || (request.StartTick == newest->StartTick && request.Id.Sequence > newest->Id.Sequence))
            newest = &request;
    }
    return newest;
}

int ResolveAnimSlotRow(const AnimBoundRig& rig, GameplayTagId behavior, const AnimPredicateInputs& inputs)
{
    if (!behavior.IsValid())
        return -1;
    for (std::size_t r = 0; r < rig.SlotRows.size(); ++r)
    {
        const AnimBoundSlotRow& row = rig.SlotRows[r];
        if (row.Behavior == behavior && EvaluateAnimProgram(row.When, inputs).Passed)
            return static_cast<int>(r);
    }
    return -1;
}

void ResolveAnimEntity(const World& world, EntityId entity, const AnimBoundRig& rig,
                       std::span<const std::uint32_t> facts, const AnimSelectorState* selection,
                       AnimContentState& content, AnimTick now, double tickSeconds, AnimDecisionLog* log)
{
    const AnimRequestSet* requests =
        world.IsRegistered<AnimRequestSet>() ? world.TryGet<AnimRequestSet>(entity) : nullptr;

    AnimPredicateInputs inputs;
    inputs.Facts = facts;
    inputs.Tags = world.IsRegistered<GameplayTagContainer>() ? world.TryGet<GameplayTagContainer>(entity) : nullptr;
    inputs.Registry = world.TryGetResource<GameplayTagRegistry>();
    inputs.Requests = requests;
    inputs.Now = now;
    inputs.TickSeconds = tickSeconds;

    const bool rebound = content.BindingGeneration != rig.Generation;
    content.BindingGeneration = rig.Generation;

    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
    {
        AnimLayerContent& layer = content.Layers[l];
        inputs.LayerBit = static_cast<std::uint8_t>(1u << l);
        inputs.BehaviorStartTick = selection != nullptr ? selection->Layers[l].WinnerStartTick : layer.StartTick;

        // Indices from another binding follow their row's stable key; pinned
        // content whose row is gone cannot stay pinned, and says so.
        bool lostPin = false;
        if (rebound && layer.Row != kAnimNoContent)
        {
            std::uint16_t remapped = kAnimNoContent;
            for (std::size_t r = 0; r < rig.SlotRows.size(); ++r)
                if (rig.SlotRows[r].Key == layer.RowKey)
                    remapped = static_cast<std::uint16_t>(r);
            if (remapped == kAnimNoContent)
            {
                lostPin = layer.Pinned;
                layer.Pinned = false;
                if (lostPin)
                    Log(log, now, l, AnimDecisionCause::Anchored, AnimChangeReason::Rebound, layer);
            }
            else
            {
                layer.Content = static_cast<std::uint16_t>(rig.SlotRows[remapped].Content);
            }
            layer.Row = remapped;
        }

        const GameplayTagId behavior = AnimLayerBehavior(rig, l, selection, requests, now);
        const int row = ResolveAnimSlotRow(rig, behavior, inputs);
        const std::uint16_t resolvedRow = row >= 0 ? static_cast<std::uint16_t>(row) : kAnimNoContent;
        const std::uint16_t resolvedContent =
            row >= 0 ? static_cast<std::uint16_t>(rig.SlotRows[static_cast<std::size_t>(row)].Content) : kAnimNoContent;
        const AnimBehaviorKind kind = KindOf(rig, behavior);

        if (behavior != layer.Behavior || lostPin)
        {
            layer.Behavior = behavior;
            layer.Row = resolvedRow;
            layer.RowKey = row >= 0 ? rig.SlotRows[static_cast<std::size_t>(row)].Key : 0;
            layer.Content = resolvedContent;
            layer.StartTick = now;
            layer.StartOffsetSeconds = 0.0f;
            layer.Pinned = kind == AnimBehaviorKind::OneShot || kind == AnimBehaviorKind::Flow;
            Log(log, now, l, AnimDecisionCause::ContentChanged,
                lostPin ? AnimChangeReason::Rebound : AnimChangeReason::BehaviorChanged, layer);
        }
        else if (resolvedRow != layer.Row && !layer.Pinned)
        {
            // Same behavior, a different row: cyclic and hold content takes
            // it now, starting at the same normalized time.
            const float oldDuration = DurationOf(rig, layer.Content);
            const float normalized = oldDuration > 0.0f ? layer.TimeSeconds / oldDuration : 0.0f;
            layer.Row = resolvedRow;
            layer.RowKey = row >= 0 ? rig.SlotRows[static_cast<std::size_t>(row)].Key : 0;
            layer.Content = resolvedContent;
            layer.StartTick = now;
            layer.StartOffsetSeconds = normalized * DurationOf(rig, resolvedContent);
            Log(log, now, l, AnimDecisionCause::ContentChanged, AnimChangeReason::RowChanged, layer);
        }

        const float duration = DurationOf(rig, layer.Content);
        const double elapsed = static_cast<double>(layer.StartOffsetSeconds)
            + static_cast<double>(now >= layer.StartTick ? now - layer.StartTick : 0) * tickSeconds;
        if (layer.Content == kAnimNoContent || duration <= 0.0f)
        {
            layer.TimeSeconds = 0.0f;
            layer.ContentComplete = layer.Content != kAnimNoContent && kind != AnimBehaviorKind::Cyclic;
        }
        else if (kind == AnimBehaviorKind::Cyclic)
        {
            layer.TimeSeconds = static_cast<float>(std::fmod(elapsed, static_cast<double>(duration)));
            layer.ContentComplete = false;
        }
        else
        {
            layer.TimeSeconds = static_cast<float>(std::min(elapsed, static_cast<double>(duration)));
            layer.ContentComplete = elapsed >= static_cast<double>(duration);
        }
    }
}

void AnimContentSystem::FixedLogic(FixedLogicContext& ctx)
{
    ResolveImpl(ctx.Entities, &ctx.Partitions, ctx.Time.TickIndex, ctx.Time.DeltaSeconds);
}

void AnimContentSystem::Resolve(World& world, AnimTick now, double tickSeconds)
{
    ResolveImpl(world, nullptr, now, tickSeconds);
}

void AnimContentSystem::ResolveImpl(World& world, const StoragePartitionSet* partitions, AnimTick now,
                                    double tickSeconds)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>() || !world.IsRegistered<AnimContentState>())
        return;
    if (LastWorld != &world)
    {
        ContentQuery.reset();
        LastWorld = &world;
    }
    if (!ContentQuery.has_value())
        ContentQuery.emplace(world);

    const bool hasSmall = world.IsRegistered<AnimFacts>();
    const bool hasLarge = world.IsRegistered<AnimFactsLarge>();
    const bool hasSelection = world.IsRegistered<AnimSelectorState>();
    const bool hasLog = world.IsRegistered<AnimDecisionLog>();

    const auto visit = [&](auto& view) {
        const auto rigs = view.template Read<AnimRig>();
        auto contents = view.template Write<AnimContentState>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = bindings->Resolve(rigs[i].Rig, world);
            if (rig == nullptr || !rig->Valid)
                continue;
            const EntityId entity = view.Entity(i);
            // Facts and selection are optional: a Prop carries neither.
            std::span<const std::uint32_t> facts;
            if (const AnimFacts* small = hasSmall ? world.TryGet<AnimFacts>(entity) : nullptr)
                facts = std::span<const std::uint32_t>(small->Values, std::min(rig->Slots.size(), kAnimFactsSmall));
            else if (const AnimFactsLarge* large = hasLarge ? world.TryGet<AnimFactsLarge>(entity) : nullptr)
                facts = std::span<const std::uint32_t>(large->Values, std::min(rig->Slots.size(), kAnimFactsLarge));
            const AnimSelectorState* selection = hasSelection ? world.TryGet<AnimSelectorState>(entity) : nullptr;
            AnimDecisionLog* log = hasLog ? world.TryGet<AnimDecisionLog>(entity) : nullptr;
            ResolveAnimEntity(world, entity, *rig, facts, selection, contents[i], now, tickSeconds, log);
        }
    };
    if (partitions != nullptr)
        ContentQuery->ForEachChunkIn(*partitions, visit);
    else
        ContentQuery->ForEachChunk(visit);
}
