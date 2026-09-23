#include <anim/AnimEventSystem.h>

#include <anim/AnimContentSystem.h>
#include <anim/AnimRequests.h>
#include <app/GameContexts.h>
#include <authored/VerbDispatcher.h>
#include <ecs/StoragePartitionSet.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <cmath>

namespace
{
    // The most loops one tick may cross. Content shorter than a tick loops
    // several times per tick; content short enough to exceed this is not
    // something whose every mark could be meant.
    constexpr std::int64_t kMaxLoopsPerTick = 64;

    // Who caused what a layer plays: the request behind it, when there is one.
    EntityId LayerInstigator(const AnimBoundRig& rig, std::size_t layer, const AnimSelectorState* selection,
                             const AnimRequestSet* requests, AnimTick now)
    {
        const AnimBoundLayer& bound = rig.Layers[layer];
        if (bound.Selector < 0)
        {
            const AnimRequest* request = NewestAnimLayerRequest(requests, layer, now);
            return request != nullptr ? request->Id.Source : EntityId{};
        }
        if (selection == nullptr || requests == nullptr)
            return {};
        const AnimLayerSelection& state = selection->Layers[layer];
        if (state.LatchRequest.IsValid())
            return state.LatchRequest.Source;
        const std::vector<AnimBoundRule>& rules = rig.Selectors[static_cast<std::size_t>(bound.Selector)].Rules;
        if (state.Winner >= rules.size() || !rules[state.Winner].LatchIntent.IsValid())
            return {};
        const AnimRequest* primary = FindPrimaryAnimRequest(*requests, rules[state.Winner].LatchIntent, now,
                                                            static_cast<std::uint8_t>(1u << layer));
        return primary != nullptr ? primary->Id.Source : EntityId{};
    }

    // Seconds into the content instance at tick `tick`.
    double Elapsed(const AnimLayerContent& layer, AnimTick tick, double tickSeconds)
    {
        return static_cast<double>(layer.StartOffsetSeconds)
            + static_cast<double>(tick >= layer.StartTick ? tick - layer.StartTick : 0) * tickSeconds;
    }

    // Calls `visit(index)` for every mark of `content` inside the stretch from
    // `from` to `to` of content time, in time order: exclusive of `from`
    // unless `includeFrom`, inclusive of `to`. Cyclic content repeats its
    // marks every loop; other content has one pass.
    template <typename Visit>
    void ForEachMark(const AnimBoundContent& content, bool cyclic, double from, double to, bool includeFrom,
                     Visit&& visit)
    {
        if (content.Events.empty() || to < from)
            return;
        const double duration = static_cast<double>(content.DurationSeconds);
        const auto inside = [&](double at) { return at <= to && (at > from || (includeFrom && at == from)); };
        if (!cyclic || duration <= 0.0)
        {
            for (std::size_t e = 0; e < content.Events.size(); ++e)
                if (inside(static_cast<double>(content.Events[e].Time) * std::max(duration, 0.0)))
                    visit(e);
            return;
        }
        const auto first = static_cast<std::int64_t>(std::floor(from / duration));
        const auto last = std::min(static_cast<std::int64_t>(std::floor(to / duration)), first + kMaxLoopsPerTick);
        for (std::int64_t loop = first; loop <= last; ++loop)
            for (std::size_t e = 0; e < content.Events.size(); ++e)
                if (inside(static_cast<double>(loop) * duration
                           + static_cast<double>(content.Events[e].Time) * duration))
                    visit(e);
    }

    AnimDecisionCause CauseOf(AnimPendingKind kind)
    {
        switch (kind)
        {
        case AnimPendingKind::Clip: return AnimDecisionCause::EventCrossed;
        case AnimPendingKind::BehaviorEntered: return AnimDecisionCause::BehaviorEntered;
        case AnimPendingKind::BehaviorExited: return AnimDecisionCause::BehaviorExited;
        }
        return AnimDecisionCause::EventCrossed;
    }

    void Record(AnimDecisionLog* log, AnimTick now, std::size_t layer, const AnimLayerContent& state,
                const AnimBoundEvent& event, AnimEventOutcome outcome, VerbAdmission admission,
                AnimPendingKind kind = AnimPendingKind::Clip)
    {
        if (log == nullptr)
            return;
        AnimDecisionRecord record;
        record.Tick = now;
        record.Cause = CauseOf(kind);
        record.Layer = static_cast<std::uint8_t>(layer);
        record.Behavior = state.Behavior;
        record.Row = state.Row;
        record.Content = state.Content;
        record.EventKey = event.Key;
        record.EventOutcome = outcome;
        record.Admission = admission;
        log->Append(record);
    }
}

void CollectAnimEvents(EntityId entity, DataAssetHandle rigHandle, const AnimBoundRig& rig,
                       const AnimSelectorState* selection, const AnimRequestSet* requests,
                       AnimContentState& content, AnimTick now, double tickSeconds, AnimEventGates gates,
                       std::vector<AnimPendingEvent>& pending, std::size_t capacity, AnimDecisionLog* log)
{
    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
    {
        AnimLayerContent& layer = content.Layers[l];
        const bool sameInstance = layer.EventStartTick == layer.StartTick && layer.EventTick != kAnimNoTick
            && layer.EventTick < now;
        const AnimTick coveredThrough = layer.EventTick;
        const GameplayTagId previousBehavior = layer.EventBehavior;
        layer.EventStartTick = layer.StartTick;
        layer.EventTick = now;
        layer.EventBehavior = layer.Behavior;

        const float weight = rig.Layers[l].Weight;
        const EntityId instigator = LayerInstigator(rig, l, selection, requests, now);
        // False when the tick's queue is full; the caller records the refusal.
        const auto admit = [&](AnimPendingEvent record) {
            if (pending.size() >= capacity)
                return false;
            record.Producer = entity;
            record.Instigator = instigator;
            record.Rig = rigHandle;
            record.RigGeneration = rig.Generation;
            record.Tick = now;
            record.Layer = static_cast<std::uint8_t>(l);
            pending.push_back(record);
            return true;
        };

        // Leaving one behavior, then entering the next, before any mark of
        // the new content: exits precede entries, which precede what plays.
        if (previousBehavior != layer.Behavior)
        {
            for (const auto& [tag, kind] : { std::pair{ previousBehavior, AnimPendingKind::BehaviorExited },
                                             std::pair{ layer.Behavior, AnimPendingKind::BehaviorEntered } })
            {
                const int index = tag.IsValid() ? rig.FindBehaviorIndex(tag) : -1;
                if (index < 0)
                    continue;
                const AnimBoundBehavior& behavior = rig.Behaviors[static_cast<std::size_t>(index)];
                const std::optional<AnimBoundEvent>& event =
                    kind == AnimPendingKind::BehaviorExited ? behavior.Exited : behavior.Entered;
                if (!event.has_value() || (event->Scope == AnimEventScope::Gameplay ? !gates.Authority : !gates.Presents))
                    continue;
                AnimLayerContent logged = layer;
                logged.Behavior = tag;
                if (event->Scope == AnimEventScope::Cosmetic && weight < behavior.Policy.EventWeight)
                {
                    Record(log, now, l, logged, *event, AnimEventOutcome::BelowWeight, VerbAdmission::Accepted, kind);
                    continue;
                }
                AnimPendingEvent record;
                record.Kind = kind;
                record.Behavior = static_cast<std::uint16_t>(index);
                if (!admit(record))
                    Record(log, now, l, logged, *event, AnimEventOutcome::Fired, VerbAdmission::QueueFull, kind);
            }
        }

        if (layer.Content >= rig.Contents.size())
            continue;

        const AnimBoundContent& played = rig.Contents[layer.Content];
        if (played.Events.empty())
            continue;
        const AnimBoundBehavior* behavior = rig.FindBehavior(layer.Behavior);
        const bool cyclic = behavior == nullptr || behavior->Policy.Kind == AnimBehaviorKind::Cyclic;
        const float threshold = behavior != nullptr ? behavior->Policy.EventWeight : 0.5f;

        const auto produce = [&](std::size_t index, bool skipped) {
            const AnimBoundEvent& event = played.Events[index];
            if (event.Scope == AnimEventScope::Gameplay ? !gates.Authority : !gates.Presents)
                return;
            // The authority is never the machine catching up from replicated
            // state, so it does not skip a gameplay event: it produces it.
            if (skipped && !(event.Scope == AnimEventScope::Gameplay && gates.Authority))
            {
                Record(log, now, l, layer, event, AnimEventOutcome::Skipped, VerbAdmission::Accepted);
                return;
            }
            if (event.Scope == AnimEventScope::Cosmetic && weight < event.MinWeight.value_or(threshold))
            {
                Record(log, now, l, layer, event, AnimEventOutcome::BelowWeight, VerbAdmission::Accepted);
                return;
            }
            AnimPendingEvent record;
            record.Content = layer.Content;
            record.Event = static_cast<std::uint16_t>(index);
            if (!admit(record))
                Record(log, now, l, layer, event, AnimEventOutcome::Fired, VerbAdmission::QueueFull);
        };

        const double to = Elapsed(layer, now, tickSeconds);
        if (!sameInstance)
        {
            // Entering content: a mark at its start is crossed on entry. A
            // row change that carries normalized time across starts past its
            // offset, whose marks the previous content already crossed.
            ForEachMark(played, cyclic, static_cast<double>(layer.StartOffsetSeconds), to,
                        layer.StartOffsetSeconds <= 0.0f, [&](std::size_t e) { produce(e, false); });
            continue;
        }
        const double previous = Elapsed(layer, now - 1, tickSeconds);
        if (coveredThrough + 1 < now)
        {
            // Ticks this pass did not see: what they covered was not played.
            ForEachMark(played, cyclic, Elapsed(layer, coveredThrough, tickSeconds), previous, false,
                        [&](std::size_t e) { produce(e, true); });
        }
        ForEachMark(played, cyclic, previous, to, false, [&](std::size_t e) { produce(e, false); });
    }
}

void DrainAnimEvents(World& world, std::span<const AnimPendingEvent> pending, VerbDispatcher* dispatcher)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    const bool hasLog = world.IsRegistered<AnimDecisionLog>();
    const bool hasContent = world.IsRegistered<AnimContentState>();
    for (const AnimPendingEvent& record : pending)
    {
        AnimDecisionLog* log = hasLog ? world.TryGet<AnimDecisionLog>(record.Producer) : nullptr;
        const AnimContentState* content = hasContent ? world.TryGet<AnimContentState>(record.Producer) : nullptr;
        AnimLayerContent layer;
        if (content != nullptr)
            layer = content->Layers[record.Layer];
        layer.Content = record.Content;

        const AnimBoundRig* rig = bindings != nullptr ? bindings->Resolve(record.Rig, world) : nullptr;
        const AnimBoundEvent* found = nullptr;
        if (rig != nullptr && rig->Generation == record.RigGeneration)
        {
            if (record.Kind == AnimPendingKind::Clip)
            {
                if (record.Content < rig->Contents.size() && record.Event < rig->Contents[record.Content].Events.size())
                    found = &rig->Contents[record.Content].Events[record.Event];
            }
            else if (record.Behavior < rig->Behaviors.size())
            {
                const AnimBoundBehavior& behavior = rig->Behaviors[record.Behavior];
                layer.Behavior = behavior.Tag;
                const std::optional<AnimBoundEvent>& lifecycle =
                    record.Kind == AnimPendingKind::BehaviorEntered ? behavior.Entered : behavior.Exited;
                found = lifecycle.has_value() ? &*lifecycle : nullptr;
            }
        }
        if (found == nullptr)
        {
            AnimBoundEvent unknown;
            Record(log, record.Tick, record.Layer, layer, unknown, AnimEventOutcome::Fired, VerbAdmission::StaleBinding,
                   record.Kind);
            continue;
        }

        const AnimBoundEvent& event = *found;
        const CompiledVerbBinding* binding = rig->Bindings.Find(event.Binding);
        VerbAdmission admission = VerbAdmission::Accepted;
        if (binding == nullptr)
            admission = VerbAdmission::UnresolvedBinding;
        else if (!event.Resolved)
            admission = VerbAdmission::InvalidArguments;
        else if (dispatcher == nullptr)
            admission = VerbAdmission::Unavailable;
        else
        {
            VerbInvocationSource source;
            source.Producer = record.Producer;
            source.Instigator = record.Instigator;
            source.Tick = record.Tick;
            admission = dispatcher->Invoke(*binding, event.Inputs, source).Status;
        }
        Record(log, record.Tick, record.Layer, layer, event, AnimEventOutcome::Fired, admission, record.Kind);
    }
}

AnimEventSystem::AnimEventSystem(VerbDispatcher* dispatcher, bool presents)
    : Dispatcher(dispatcher)
    , Presents(presents)
{
}

void AnimEventSystem::FixedLogic(FixedLogicContext& ctx)
{
    RunImpl(ctx.Entities, &ctx.Partitions, ctx.Time.TickIndex, ctx.Time.DeltaSeconds);
}

void AnimEventSystem::Run(World& world, AnimTick now, double tickSeconds)
{
    RunImpl(world, nullptr, now, tickSeconds);
}

void AnimEventSystem::RunImpl(World& world, const StoragePartitionSet* partitions, AnimTick now, double tickSeconds)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>() || !world.IsRegistered<AnimContentState>())
        return;
    if (LastWorld != &world)
    {
        EventQuery.reset();
        LastWorld = &world;
    }
    if (!EventQuery.has_value())
        EventQuery.emplace(world);

    const AnimEventGates gates{ .Authority = IsSimulationAuthority(world), .Presents = Presents };
    const bool hasSelection = world.IsRegistered<AnimSelectorState>();
    const bool hasRequests = world.IsRegistered<AnimRequestSet>();
    const bool hasLog = world.IsRegistered<AnimDecisionLog>();

    Pending.clear();
    const auto visit = [&](auto& view) {
        const auto rigs = view.template Read<AnimRig>();
        auto contents = view.template Write<AnimContentState>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = bindings->Resolve(rigs[i].Rig, world);
            if (rig == nullptr || !rig->Valid)
                continue;
            const EntityId entity = view.Entity(i);
            CollectAnimEvents(entity, rigs[i].Rig, *rig,
                              hasSelection ? world.TryGet<AnimSelectorState>(entity) : nullptr,
                              hasRequests ? world.TryGet<AnimRequestSet>(entity) : nullptr, contents[i], now,
                              tickSeconds, gates, Pending, Capacity,
                              hasLog ? world.TryGet<AnimDecisionLog>(entity) : nullptr);
        }
    };
    if (partitions != nullptr)
        EventQuery->ForEachChunkIn(*partitions, visit);
    else
        EventQuery->ForEachChunk(visit);

    // After the query has finished: an operation the drain reaches may change
    // what the World holds.
    DrainAnimEvents(world, Pending, Dispatcher);
    Pending.clear();
}
