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
    // Content shorter than a tick loops several times per tick; past this many loops,
    // its marks cannot all be meant.
    constexpr std::int64_t kMaxLoopsPerTick = 64;

    double Elapsed(const AnimLayerContent& layer, AnimTick tick, double tickSeconds)
    {
        return static_cast<double>(layer.ClipOffsetSeconds)
            + static_cast<double>(tick >= layer.ClipStartTick ? tick - layer.ClipStartTick : 0) * tickSeconds
                * static_cast<double>(layer.ClipRate);
    }

    // Marks of `content` from `from` to `to`, in time order: exclusive of `from` unless
    // `includeFrom`, inclusive of `to`. Cyclic content repeats its marks every loop.
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

    std::uint8_t SectionOf(const AnimPendingEvent& record)
    {
        return record.Kind == AnimPendingKind::SectionEntered || record.Kind == AnimPendingKind::SectionExited
            ? static_cast<std::uint8_t>(record.Event)
            : kAnimNoSection;
    }

    AnimDecisionCause CauseOf(AnimPendingKind kind)
    {
        switch (kind)
        {
        case AnimPendingKind::Clip: return AnimDecisionCause::EventCrossed;
        case AnimPendingKind::BehaviorEntered: return AnimDecisionCause::BehaviorEntered;
        case AnimPendingKind::BehaviorExited: return AnimDecisionCause::BehaviorExited;
        case AnimPendingKind::SectionEntered: return AnimDecisionCause::SectionEntered;
        case AnimPendingKind::SectionExited: return AnimDecisionCause::SectionExited;
        }
        return AnimDecisionCause::EventCrossed;
    }

    void Record(AnimDecisionLog* log, AnimTick now, std::size_t layer, const AnimLayerContent& state,
                const AnimBoundEvent& event, AnimEventOutcome outcome, VerbAdmission admission,
                AnimPendingKind kind = AnimPendingKind::Clip, std::uint8_t section = 0xFF)
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
        record.Section = section;
        log->Append(record);
    }
}

void CollectAnimEvents(EntityId entity, DataAssetHandle rigHandle, const AnimBoundRig& rig,
                       const AnimSelectorState* selection, const AnimRequestSet* requests,
                       const AnimFlowState* flows, AnimContentState& content, AnimTick now, double tickSeconds,
                       AnimEventGates gates, std::vector<AnimPendingEvent>& pending, std::size_t capacity,
                       AnimDecisionLog* log)
{
    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
    {
        AnimLayerContent& layer = content.Layers[l];
        const bool sameClip = layer.EventStartTick == layer.ClipStartTick && layer.EventTick != kAnimNoTick
            && layer.EventTick < now;
        const AnimTick coveredThrough = layer.EventTick;
        const GameplayTagId previousBehavior = layer.EventBehavior;
        const std::uint16_t previousContent = layer.EventContent;
        const std::uint8_t previousSection = layer.EventSection;
        const AnimTick previousContentStart = layer.EventContentStartTick;
        const float previousPhase = layer.EventPhase;
        layer.EventPhase = layer.Phase;

        const AnimLayerFlow* flow = flows != nullptr ? &flows->Layers[l] : nullptr;
        const bool playingFlow = flow != nullptr && flow->Phase != AnimFlowPhase::None
            && layer.Content < rig.Contents.size() && rig.Contents[layer.Content].Flow >= 0;
        const std::uint8_t section = playingFlow ? flow->Section : kAnimNoSection;

        layer.EventStartTick = layer.ClipStartTick;
        layer.EventTick = now;
        layer.EventBehavior = layer.Behavior;
        layer.EventContent = playingFlow ? layer.Content : kAnimNoContent;
        layer.EventSection = section;
        layer.EventContentStartTick = layer.StartTick;

        const float weight = AnimLayerWeight(rig, l, selection);
        const AnimRequest* driving = AnimLayerDrivingRequest(rig, l, selection, requests, now, &layer);
        const EntityId instigator = driving != nullptr ? driving->Id.Source : EntityId{};
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
        // A lifecycle event: gated by scope, suppressed below the weight a
        // cosmetic one needs, refused past capacity, otherwise queued.
        const auto lifecycle = [&](const std::optional<AnimBoundEvent>& event, AnimPendingEvent record,
                                   AnimLayerContent logged, float threshold) {
            if (!event.has_value() || (event->Scope == AnimEventScope::Gameplay ? !gates.Authority : !gates.Presents))
                return;
            const bool isSection = record.Kind == AnimPendingKind::SectionEntered
                || record.Kind == AnimPendingKind::SectionExited;
            const std::uint8_t sectionIndex = isSection ? static_cast<std::uint8_t>(record.Event) : kAnimNoSection;
            if (event->Scope == AnimEventScope::Cosmetic && weight < threshold)
            {
                Record(log, now, l, logged, *event, AnimEventOutcome::BelowWeight, VerbAdmission::Accepted, record.Kind,
                       sectionIndex);
                return;
            }
            if (!admit(record))
                Record(log, now, l, logged, *event, AnimEventOutcome::Fired, VerbAdmission::QueueFull, record.Kind,
                       sectionIndex);
        };
        const auto sectionEvent = [&](std::uint16_t contentIndex, std::uint8_t sectionIndex, AnimPendingKind kind) {
            if (contentIndex >= rig.Contents.size() || rig.Contents[contentIndex].Flow < 0)
                return;
            const AnimBoundFlow& bound = rig.Flows[static_cast<std::size_t>(rig.Contents[contentIndex].Flow)];
            if (sectionIndex >= bound.Sections.size())
                return;
            const AnimBoundFlowSection& target = bound.Sections[sectionIndex];
            AnimPendingEvent record;
            record.Kind = kind;
            record.Content = contentIndex;
            record.Event = sectionIndex;
            AnimLayerContent logged = layer;
            logged.Content = contentIndex;
            const AnimBoundBehavior* owner = rig.FindBehavior(layer.Behavior);
            lifecycle(kind == AnimPendingKind::SectionEntered ? target.Entered : target.Exited, record, logged,
                      owner != nullptr ? owner->Policy.EventWeight : 0.5f);
        };
        const auto behaviorEvent = [&](GameplayTagId tag, AnimPendingKind kind) {
            const int index = tag.IsValid() ? rig.FindBehaviorIndex(tag) : -1;
            if (index < 0)
                return;
            const AnimBoundBehavior& behavior = rig.Behaviors[static_cast<std::size_t>(index)];
            AnimPendingEvent record;
            record.Kind = kind;
            record.Behavior = static_cast<std::uint16_t>(index);
            AnimLayerContent logged = layer;
            logged.Behavior = tag;
            lifecycle(kind == AnimPendingKind::BehaviorExited ? behavior.Exited : behavior.Entered, record, logged,
                      behavior.Policy.EventWeight);
        };

        // Outermost last on the way out and first on the way in: the old section is left
        // before its behavior, the new behavior entered before its section, all before
        // any mark of what now plays.
        const bool behaviorChanged = previousBehavior != layer.Behavior;
        const bool sectionChanged = previousSection != section || previousContent != layer.EventContent
            || (playingFlow && previousContentStart != layer.StartTick);
        if (sectionChanged && previousSection != kAnimNoSection)
            sectionEvent(previousContent, previousSection, AnimPendingKind::SectionExited);
        if (behaviorChanged)
        {
            behaviorEvent(previousBehavior, AnimPendingKind::BehaviorExited);
            behaviorEvent(layer.Behavior, AnimPendingKind::BehaviorEntered);
        }
        if (sectionChanged && section != kAnimNoSection)
            sectionEvent(layer.Content, section, AnimPendingKind::SectionEntered);

        if (layer.Clip >= rig.Contents.size())
            continue;
        const AnimBoundContent& played = rig.Contents[layer.Clip];
        if (played.Events.empty())
            continue;
        const AnimBoundBehavior* behavior = rig.FindBehavior(layer.Behavior);
        // A flow's section plays its clip once per section instance; a
        // behavior's own clip loops when the behavior is cyclic.
        const bool cyclic = !playingFlow && (behavior == nullptr || behavior->Policy.Kind == AnimBehaviorKind::Cyclic);
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
            record.Content = layer.Clip;
            record.Event = static_cast<std::uint16_t>(index);
            if (!admit(record))
                Record(log, now, l, layer, event, AnimEventOutcome::Fired, VerbAdmission::QueueFull);
        };

        // A blendspace's marks are its heaviest sample's, crossed in its shared
        // phase: the stretch since the last pass, unwrapped across a loop.
        if (layer.Content < rig.Contents.size() && rig.Contents[layer.Content].Blendspace >= 0)
        {
            const double length = static_cast<double>(played.DurationSeconds);
            const double at = static_cast<double>(layer.Phase) * length;
            if (!sameClip)
            {
                ForEachMark(played, cyclic, at, at, layer.Phase <= 0.0f, [&](std::size_t e) { produce(e, false); });
                continue;
            }
            const double from = static_cast<double>(previousPhase) * length;
            const double until = cyclic && layer.Phase < previousPhase ? at + length : at;
            // Where the phase stood on ticks the pass did not see is not
            // recorded, so a gap skips the whole stretch.
            const bool skipped = coveredThrough + 1 < now;
            ForEachMark(played, cyclic, from, until, false, [&](std::size_t e) { produce(e, skipped); });
            continue;
        }

        const double to = Elapsed(layer, now, tickSeconds);
        if (!sameClip)
        {
            // A mark at a clip's start is crossed on entry. A row change carrying normalized
            // time starts past its offset, whose marks the previous content crossed.
            ForEachMark(played, cyclic, static_cast<double>(layer.ClipOffsetSeconds), to,
                        layer.ClipOffsetSeconds <= 0.0f, [&](std::size_t e) { produce(e, false); });
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
        const AnimContentState* content =
            hasContent ? static_cast<const World&>(world).TryGet<AnimContentState>(record.Producer) : nullptr;
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
            else if (record.Kind == AnimPendingKind::SectionEntered || record.Kind == AnimPendingKind::SectionExited)
            {
                if (record.Content < rig->Contents.size() && rig->Contents[record.Content].Flow >= 0)
                {
                    const AnimBoundFlow& flow = rig->Flows[static_cast<std::size_t>(rig->Contents[record.Content].Flow)];
                    if (record.Event < flow.Sections.size())
                    {
                        const AnimBoundFlowSection& section = flow.Sections[record.Event];
                        const std::optional<AnimBoundEvent>& lifecycle =
                            record.Kind == AnimPendingKind::SectionEntered ? section.Entered : section.Exited;
                        found = lifecycle.has_value() ? &*lifecycle : nullptr;
                    }
                }
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
                   record.Kind, SectionOf(record));
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
        Record(log, record.Tick, record.Layer, layer, event, AnimEventOutcome::Fired, admission, record.Kind,
               SectionOf(record));
    }
}

AnimEventSystem::AnimEventSystem(VerbDispatcher* dispatcher, bool presents)
    : Dispatcher(dispatcher)
    , Presents(presents)
{
}

void AnimEventSystem::FixedLogic(FixedLogicContext& ctx)
{
    RunImpl(ctx.Entities, &ctx.Partitions, AuthorityTickOf(ctx.Entities, ctx.Time.TickIndex), ctx.Time.DeltaSeconds);
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
    const bool hasFlows = world.IsRegistered<AnimFlowState>();

    Pending.clear();
    const auto visit = [&](auto& view) {
        const auto rigs = view.template Read<AnimRig>();
        auto contents = view.template Write<AnimContentState>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = bindings->Resolve(rigs[i].Rig, world);
            if (rig == nullptr || !rig->Valid || !ShouldRunAnimationLogic(Presents, *rig))
                continue;
            const EntityId entity = view.Entity(i);
            const World& reader = world;
            CollectAnimEvents(entity, rigs[i].Rig, *rig,
                              hasSelection ? reader.TryGet<AnimSelectorState>(entity) : nullptr,
                              hasRequests ? reader.TryGet<AnimRequestSet>(entity) : nullptr,
                              hasFlows ? reader.TryGet<AnimFlowState>(entity) : nullptr, contents[i], now,
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
