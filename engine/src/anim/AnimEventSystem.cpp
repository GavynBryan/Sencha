#include <anim/AnimEventSystem.h>

#include <anim/AnimContentSystem.h>
#include <anim/AnimPlayback.h>
#include <anim/AnimRequests.h>
#include <app/GameContexts.h>
#include <authored/VerbDispatcher.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <cmath>

namespace
{
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
                       const AnimFlowState* flows, const AnimContentState& content, AnimEventCursor& cursor,
                       AnimTick now, double tickSeconds, AnimEventGates gates, AnimPendingEvents& pending,
                       AnimDecisionLog* log)
{
    // Indices the pass kept from another binding name other content now: across a
    // rebind an instance is known by its placement alone, and nothing is announced.
    const bool rebound = cursor.BindingGeneration != 0 && cursor.BindingGeneration != rig.Generation;
    cursor.BindingGeneration = rig.Generation;
    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
    {
        const AnimLayerContent& layer = content.Layers[l];
        AnimLayerEventCursor& seen = cursor.Layers[l];
        const AnimTick through = seen.Tick;
        const AnimPlayback previousPlayback = seen.Playback;
        const std::uint16_t previousClip = seen.Clip;
        const GameplayTagId previousBehavior = seen.Behavior;
        const AnimRequestId previousRequest = seen.Request;
        const std::uint16_t previousContent = seen.Content;
        const std::uint8_t previousSection = seen.Section;
        const AnimTick previousContentStart = seen.ContentStartTick;
        const float previousPhase = seen.Phase;

        const AnimLayerFlow* flow = flows != nullptr ? &flows->Layers[l] : nullptr;
        const bool playingFlow = flow != nullptr && flow->Phase != AnimFlowPhase::None
            && layer.Content < rig.Contents.size() && rig.Contents[layer.Content].Flow >= 0;
        const bool playingMix = layer.Content < rig.Contents.size() && rig.Contents[layer.Content].Blendspace >= 0;
        const std::uint8_t section = playingFlow ? flow->Section : kAnimNoSection;

        // One content instance runs on while its content and start hold; a clip instance
        // while its clip and placement do. A mix's instance is its content's, whichever
        // sample is heaviest.
        const bool sameContent = through != kAnimNoTick && (rebound || previousContent == layer.Content)
            && previousContentStart == layer.StartTick;
        const bool sameInstance = playingMix
            ? sameContent
            : through != kAnimNoTick && (rebound || previousClip == layer.Clip)
                && previousPlayback.StartTick == layer.Playback.StartTick
                && previousPlayback.OffsetSeconds == layer.Playback.OffsetSeconds;
        // A flow that moved to another section, or looped one, within the same instance
        // played the one it left until it left it.
        const bool sectionPlayedOut = !rebound && playingFlow && sameContent && !sameInstance;

        // A pass never moves an instance's coverage backwards: a repeated or earlier tick
        // leaves it where it was.
        if (!sameInstance || now > through)
        {
            seen.Tick = now;
            seen.Phase = layer.Phase;
        }
        seen.Playback = layer.Playback;
        seen.Clip = layer.Clip;
        seen.Behavior = layer.Behavior;
        seen.Request = layer.Request;
        seen.Content = layer.Content;
        seen.Section = section;
        seen.ContentStartTick = layer.StartTick;

        const float weight = AnimLayerWeight(rig, l, selection);
        // A request's source instigated what it drives, and its cause is the parent of
        // what that plays; what a layer leaves was the previous request's.
        const auto causeOf = [&](AnimRequestId id) {
            if (requests != nullptr && id.IsValid())
                for (const AnimRequest& request : requests->Records)
                    if (request.Occupied && request.Id == id)
                        return request.Cause;
            return InvocationId{};
        };
        // False when the scope's queue is full this tick; the caller records the refusal.
        const auto admit = [&](AnimPendingEvent record, AnimEventScope scope) {
            if (!pending.Admit(scope))
                return false;
            const bool leaving = record.Kind == AnimPendingKind::BehaviorExited
                              || record.Kind == AnimPendingKind::SectionExited;
            const AnimRequestId request = leaving ? previousRequest : layer.Request;
            record.Producer = entity;
            record.Instigator = request.Source;
            record.Cause = causeOf(request);
            record.Rig = rigHandle;
            record.RigGeneration = rig.Generation;
            record.Tick = now;
            record.Layer = static_cast<std::uint8_t>(l);
            pending.Queue(scope).push_back(record);
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
            if (!admit(record, event->Scope))
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
        const bool sectionChanged = !rebound
            && (previousSection != section || previousContent != seen.Content
                || (playingFlow && previousContentStart != layer.StartTick));
        if (sectionChanged && previousSection != kAnimNoSection)
            sectionEvent(previousContent, previousSection, AnimPendingKind::SectionExited);
        if (behaviorChanged)
        {
            behaviorEvent(previousBehavior, AnimPendingKind::BehaviorExited);
            behaviorEvent(layer.Behavior, AnimPendingKind::BehaviorEntered);
        }
        if (sectionChanged && section != kAnimNoSection)
            sectionEvent(layer.Content, section, AnimPendingKind::SectionEntered);

        // Crosses marks of the clip at `clipIndex` inside `stretch`.
        const auto crossMarks = [&](std::uint16_t clipIndex, const AnimPlayback& playback,
                                    const AnimCrossedStretch& stretch) {
            if (clipIndex >= rig.Contents.size())
                return;
            const AnimBoundContent& played = rig.Contents[clipIndex];
            const AnimBoundBehavior* behavior = rig.FindBehavior(layer.Behavior);
            const float threshold = behavior != nullptr ? behavior->Policy.EventWeight : 0.5f;
            const double length = static_cast<double>(played.DurationSeconds);
            ForEachAnimMark(
                playback, stretch, played.Events.size(),
                [&](std::size_t e) { return static_cast<double>(played.Events[e].Time) * std::max(length, 0.0); },
                [&](std::size_t e) {
                    const AnimBoundEvent& event = played.Events[e];
                    if (event.Scope == AnimEventScope::Gameplay ? !gates.Authority : !gates.Presents)
                        return;
                    AnimLayerContent logged = layer;
                    logged.Clip = clipIndex;
                    // Passed over by a jump, on any machine and at any scope: nothing replays
                    // marks nobody saw as they were reached.
                    if (stretch.Skipped)
                    {
                        Record(log, now, l, logged, event, AnimEventOutcome::Skipped, VerbAdmission::Accepted);
                        return;
                    }
                    if (event.Scope == AnimEventScope::Cosmetic && weight < event.MinWeight.value_or(threshold))
                    {
                        Record(log, now, l, logged, event, AnimEventOutcome::BelowWeight, VerbAdmission::Accepted);
                        return;
                    }
                    AnimPendingEvent record;
                    record.Content = clipIndex;
                    record.Event = static_cast<std::uint16_t>(e);
                    if (!admit(record, event.Scope))
                        Record(log, now, l, logged, event, AnimEventOutcome::Fired, VerbAdmission::QueueFull);
                });
        };

        if (sectionPlayedOut)
        {
            const AnimCrossing tail =
                AnimCrossPlayback(previousPlayback, through, AnimPlaybackEntry::Start, now, tickSeconds);
            for (std::uint8_t s = 0; s < tail.Count; ++s)
                crossMarks(previousClip, previousPlayback, tail.Stretches[s]);
        }

        if (layer.Clip >= rig.Contents.size() || rig.Contents[layer.Clip].Events.empty())
            continue;

        if (playingMix)
        {
            // A mix's marks are its heaviest sample's, crossed in its shared phase: the
            // stretch since the last pass, unwrapped across a loop. Where the phase stood
            // on ticks no pass saw is not recorded, so a gap skips the whole stretch.
            const AnimPlayback phase{ .StartTick = layer.StartTick, .OffsetSeconds = 0.0f, .Rate = 1.0f,
                                      .DurationSeconds = rig.Contents[layer.Clip].DurationSeconds,
                                      .Cyclic = layer.Playback.Cyclic };
            const double length = static_cast<double>(phase.DurationSeconds);
            const double at = static_cast<double>(layer.Phase) * length;
            if (!sameInstance)
            {
                crossMarks(layer.Clip, phase, AnimCrossedStretch{ at, at, layer.Phase <= 0.0f, now > layer.StartTick + 1 });
                continue;
            }
            if (now <= through)
                continue;
            const double from = static_cast<double>(previousPhase) * length;
            const double until = phase.Cyclic && layer.Phase < previousPhase ? at + length : at;
            crossMarks(layer.Clip, phase, AnimCrossedStretch{ from, until, false, through + 1 < now });
            continue;
        }

        const AnimCrossing crossing = AnimCrossPlayback(
            layer.Playback, sameInstance ? through : kAnimNoTick,
            layer.Carried ? AnimPlaybackEntry::Carried : AnimPlaybackEntry::Start, now, tickSeconds);
        for (std::uint8_t s = 0; s < crossing.Count; ++s)
            crossMarks(layer.Clip, layer.Playback, crossing.Stretches[s]);
    }
}

bool AnimPendingEvents::Admit(AnimEventScope scope)
{
    const bool gameplay = scope == AnimEventScope::Gameplay;
    if ((gameplay ? Gameplay.size() : Cosmetic.size()) < (gameplay ? GameplayCapacity : CosmeticCapacity))
        return true;
    ++(gameplay ? GameplayRefused : CosmeticRefused);
    return false;
}

void AnimPendingEvents::Clear()
{
    Gameplay.clear();
    Cosmetic.clear();
    GameplayRefused = 0;
    CosmeticRefused = 0;
}

namespace
{
    void Drain(World& world, std::span<const AnimPendingEvent> pending, VerbDispatcher* dispatcher);
}

void DrainAnimEvents(World& world, const AnimPendingEvents& pending, VerbDispatcher* dispatcher)
{
    Drain(world, pending.Gameplay, dispatcher);
    Drain(world, pending.Cosmetic, dispatcher);
}

namespace
{
void Drain(World& world, std::span<const AnimPendingEvent> pending, VerbDispatcher* dispatcher)
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
            source.Parent = record.Cause;
            source.Tick = record.Tick;
            admission = dispatcher->Invoke(*binding, event.Inputs, source).Status;
        }
        Record(log, record.Tick, record.Layer, layer, event, AnimEventOutcome::Fired, admission, record.Kind,
               SectionOf(record));
    }
}
}

AnimEventSystem::AnimEventSystem(VerbDispatcher* dispatcher, bool presents, LoggingProvider* logging)
    : Dispatcher(dispatcher)
    , Presents(presents)
    , Logging(logging)
{
}

void AnimEventSystem::SetCapacity(AnimEventScope scope, std::size_t capacity)
{
    (scope == AnimEventScope::Gameplay ? Pending.GameplayCapacity : Pending.CosmeticCapacity) = capacity;
}

std::size_t AnimEventSystem::GetCapacity(AnimEventScope scope) const
{
    return scope == AnimEventScope::Gameplay ? Pending.GameplayCapacity : Pending.CosmeticCapacity;
}

void AnimEventSystem::FixedLogic(FixedLogicContext& ctx)
{
    RunImpl(ctx.Entities, &ctx.Partitions, AnimClockAt(ctx.Entities, ctx.Time.TickIndex), ctx.Time.DeltaSeconds);
}

void AnimEventSystem::Run(World& world, AnimTick now, double tickSeconds)
{
    RunImpl(world, nullptr, AnimClock::Uniform(now), tickSeconds);
}

void AnimEventSystem::RunImpl(World& world, const StoragePartitionSet* partitions, const AnimClock& clock,
                              double tickSeconds)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>() || !world.IsRegistered<AnimContentState>()
        || !world.IsRegistered<AnimEventCursor>())
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

    Pending.Clear();
    AnimRigRunCache resolver(*bindings, world);
    const auto visit = [&](auto& view) {
        const auto rigs = view.template Read<AnimRig>();
        const auto contents = view.template Read<AnimContentState>();
        auto cursors = view.template Write<AnimEventCursor>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = resolver.Resolve(rigs[i].Rig);
            if (rig == nullptr || !rig->Valid || !ShouldRunAnimationLogic(Presents, *rig))
                continue;
            const EntityId entity = view.Entity(i);
            const World& reader = world;
            CollectAnimEvents(entity, rigs[i].Rig, *rig,
                              hasSelection ? reader.TryGet<AnimSelectorState>(entity) : nullptr,
                              hasRequests ? reader.TryGet<AnimRequestSet>(entity) : nullptr,
                              hasFlows ? reader.TryGet<AnimFlowState>(entity) : nullptr, contents[i], cursors[i],
                              clock.For(entity),
                              tickSeconds, gates, Pending,
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

    TotalGameplayRefused += Pending.GameplayRefused;
    TotalCosmeticRefused += Pending.CosmeticRefused;
    // A refused gameplay event is a lost effect, logged whether or not anyone traces the
    // entity; cosmetic refusals are the documented bound and only counted.
    if (Pending.GameplayRefused > 0 && !Overflowing && Logging != nullptr)
        Logging->GetLogger<AnimEventSystem>().Error(
            "{} gameplay animation events at tick {} found their queue full ({}); raise "
            "anim.events.gameplay_capacity or spread the content's gameplay marks.",
            Pending.GameplayRefused, clock.Now, Pending.GameplayCapacity);
    Overflowing = Pending.GameplayRefused > 0;
    Pending.Clear();
}
