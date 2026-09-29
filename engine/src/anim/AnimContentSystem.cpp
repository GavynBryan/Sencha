#include <anim/AnimContentSystem.h>

#include <anim/AnimBlendspace.h>
#include <anim/AnimBlendspaceData.h>
#include <anim/AnimFacts.h>
#include <anim/AnimFlowRunner.h>
#include <anim/AnimRequests.h>
#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

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

    bool IsCyclic(AnimBehaviorKind kind) { return kind == AnimBehaviorKind::Cyclic; }

    // A behavior the rig does not know plays as authored.
    const AnimBehaviorDecl& PlaybackOf(const AnimBoundRig& rig, GameplayTagId behavior)
    {
        static const AnimBehaviorDecl asAuthored;
        const AnimBoundBehavior* bound = rig.FindBehavior(behavior);
        return bound != nullptr ? bound->Policy : asAuthored;
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

float AnimLayerWeight(const AnimBoundRig& rig, std::size_t layer, const AnimSelectorState* selection)
{
    if (rig.Layers[layer].Selector >= 0 && selection != nullptr && selection->Evaluated)
        return selection->Layers[layer].Weight;
    return rig.Layers[layer].Weight;
}

const AnimRequest* AnimLayerDrivingRequest(const AnimBoundRig& rig, std::size_t layer,
                                           const AnimSelectorState* selection, const AnimRequestSet* requests,
                                           AnimTick now, const AnimLayerContent* playing)
{
    if (requests == nullptr)
        return nullptr;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << layer);
    const auto newer = [](const AnimRequest& a, const AnimRequest* b) {
        return b == nullptr || IsNewerAnimRequest(a, *b);
    };

    const AnimBoundLayer& bound = rig.Layers[layer];
    if (bound.Selector < 0)
    {
        const AnimRequest* live = nullptr;
        for (const AnimRequest& request : requests->Records)
            if ((request.Layers & bit) != 0 && IsAnimRequestLive(request, now) && newer(request, live))
                live = &request;
        if (live != nullptr)
            return live;
        const bool playingFlow = playing != nullptr && playing->Request.IsValid() && !playing->ContentComplete
                              && playing->Content < rig.Contents.size() && rig.Contents[playing->Content].Flow >= 0;
        if (playingFlow)
        {
            // A flow still playing its cancelled request out keeps it.
            const AnimBoundBehavior* behavior = rig.FindBehavior(playing->Behavior);
            if (behavior != nullptr && behavior->Policy.Latch.OnRequestCancel == AnimRequestCancelAction::Abort)
                return nullptr;
            for (const AnimRequest& request : requests->Records)
                if (request.Occupied && request.Id == playing->Request && request.IsCancelled()
                    && IsAnimRequestRetained(request, now))
                    return &request;
            return nullptr;
        }
        // A tail kept past the cancel tick means a flow is still playing the request out,
        // which a machine that joined meanwhile adopts.
        const AnimRequest* tail = nullptr;
        for (const AnimRequest& request : requests->Records)
            if ((request.Layers & bit) != 0 && request.Occupied && request.IsCancelled()
                && request.TailUntilTick > request.CancelTick && IsAnimRequestRetained(request, now)
                && newer(request, tail))
                tail = &request;
        if (tail == nullptr)
            return nullptr;
        const AnimBoundBehavior* behavior = rig.FindBehavior(tail->Intent);
        return behavior != nullptr && behavior->Policy.Latch.OnRequestCancel != AnimRequestCancelAction::Abort
                 ? tail
                 : nullptr;
    }

    if (selection == nullptr)
        return nullptr;
    const AnimLayerSelection& state = selection->Layers[layer];
    if (state.LatchRequest.IsValid())
    {
        for (const AnimRequest& request : requests->Records)
            if (request.Occupied && request.Id == state.LatchRequest && IsAnimRequestRetained(request, now))
                return &request;
        return nullptr;
    }
    const std::vector<AnimBoundRule>& rules = rig.Selectors[static_cast<std::size_t>(bound.Selector)].Rules;
    if (state.Winner >= rules.size() || !rules[state.Winner].LatchIntent.IsValid())
        return nullptr;
    return FindPrimaryAnimRequest(*requests, rules[state.Winner].LatchIntent, now, bit);
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

namespace
{
    const AnimRequest* CancelledAnimRequest(const AnimRequestSet* requests, AnimRequestId id)
    {
        if (requests == nullptr || !id.IsValid())
            return nullptr;
        for (const AnimRequest& request : requests->Records)
            if (request.Occupied && request.Id == id && request.IsCancelled())
                return &request;
        return nullptr;
    }

    // What this tick asks a layer to play.
    struct LayerTarget
    {
        const AnimRequest* Driving = nullptr;
        GameplayTagId Behavior;
        AnimBehaviorKind Kind = AnimBehaviorKind::Cyclic;
        std::uint16_t Row = kAnimNoContent;
        std::uint32_t RowKey = 0;
        std::uint16_t Content = kAnimNoContent;
        // Where a new instance starts: somewhere every machine can place it.
        AnimTick InstanceStart = 0;
    };

    enum class LayerInstanceChange : std::uint8_t
    {
        Continue,
        Start,
        // Starts at the outgoing content's normalized time.
        StartCarried,
        // The same content runs on under a superseding request.
        Adopt,
        // Cyclic or hold content takes another row at the same normalized time.
        ChangeRow,
    };

    struct LayerInstanceDecision
    {
        LayerInstanceChange Change = LayerInstanceChange::Continue;
        AnimChangeReason Reason = AnimChangeReason::None;
    };

    struct LayerEntry
    {
        bool Entered = false;
        bool Adopted = false;
        // Set when content starts this tick: the normalized time it starts at.
        std::optional<float> StartNormalized;
    };

    struct LayerRequestProgress
    {
        AnimRequestId Tail;
        AnimRequestId Anchored;
        std::uint8_t AnchorSection = kAnimNoAnchorSection;
        AnimTick AnchorSectionStartTick = 0;
    };

    // What resolution writes back into gameplay's request set, applied once after every
    // layer has read the set as it stood.
    struct AnimRequestProgress
    {
        bool StampTiming = false;
        LayerRequestProgress Layers[kAnimMaxLayers] = {};
    };

    GameplayTagId LayerBehavior(const AnimBoundRig& rig, std::size_t layer, const AnimSelectorState* selection,
                                const AnimRequest* driving)
    {
        const AnimBoundLayer& bound = rig.Layers[layer];
        if (bound.Selector >= 0)
            return selection != nullptr && selection->Layers[layer].Winner != kAnimNoRule
                     ? selection->Layers[layer].Behavior
                     : bound.Idle;
        return driving != nullptr ? driving->Intent : bound.Idle;
    }

    // The authority says which timing its requests were made under; a machine binding
    // the rig differently cannot reconstruct them, and says so.
    void NoteRigTiming(const AnimBoundRig& rig, const AnimRequestSet& requests, bool authority,
                       AnimContentState& content, AnimRequestProgress& progress, AnimTick now, AnimDecisionLog* log)
    {
        if (authority)
        {
            progress.StampTiming = requests.RigTiming != rig.TimingIdentity;
            return;
        }
        const bool disagrees = requests.RigTiming != 0 && requests.RigTiming != rig.TimingIdentity;
        if (disagrees == content.TimingDisagrees)
            return;
        content.TimingDisagrees = disagrees;
        if (log != nullptr)
        {
            AnimDecisionRecord record;
            record.Tick = now;
            record.Cause = disagrees ? AnimDecisionCause::TimingDisagreed : AnimDecisionCause::TimingAgreed;
            log->Append(record);
        }
    }

    // Indices from another binding follow their row's stable key. True when pinned
    // content lost its row, which cannot stay pinned.
    bool RemapLayerAfterRebind(const AnimBoundRig& rig, std::size_t l, AnimLayerContent& layer, AnimTick now,
                               AnimDecisionLog* log)
    {
        if (layer.Row == kAnimNoContent)
            return false;
        std::uint16_t remapped = kAnimNoContent;
        for (std::size_t r = 0; r < rig.SlotRows.size(); ++r)
            if (rig.SlotRows[r].Key == layer.RowKey)
                remapped = static_cast<std::uint16_t>(r);
        bool lostPin = false;
        if (remapped == kAnimNoContent)
        {
            // Its content went with the row; nothing plays until a row resolves.
            layer.Content = kAnimNoContent;
            lostPin = layer.Pinned;
            layer.Pinned = false;
            if (lostPin)
                Log(log, now, l, AnimDecisionCause::IndexReset, AnimChangeReason::Rebound, layer);
        }
        else
        {
            layer.Content = static_cast<std::uint16_t>(rig.SlotRows[remapped].Content);
        }
        layer.Row = remapped;
        return lostPin;
    }

    LayerTarget ResolveLayerTarget(const AnimBoundRig& rig, std::size_t l, const AnimSelectorState* selection,
                                   const AnimRequestSet* requests, const AnimPredicateInputs& inputs,
                                   const AnimLayerContent& layer, AnimTick now)
    {
        LayerTarget target;
        target.Driving = AnimLayerDrivingRequest(rig, l, selection, requests, now, &layer);
        target.Behavior = LayerBehavior(rig, l, selection, target.Driving);
        target.Kind = KindOf(rig, target.Behavior);
        if (const int row = ResolveAnimSlotRow(rig, target.Behavior, inputs); row >= 0)
        {
            const AnimBoundSlotRow& bound = rig.SlotRows[static_cast<std::size_t>(row)];
            target.Row = static_cast<std::uint16_t>(row);
            target.RowKey = bound.Key;
            target.Content = static_cast<std::uint16_t>(bound.Content);
        }
        // The driving request's start, or the cancel that cut the last content short;
        // otherwise this tick. See docs/gameplay/animation.md.
        const AnimRequest* cutShort = target.Driving == nullptr && !layer.ContentComplete
                                        ? CancelledAnimRequest(requests, layer.Request)
                                        : nullptr;
        target.InstanceStart = target.Driving != nullptr ? std::min(target.Driving->StartTick, now)
                             : cutShort != nullptr       ? std::min(cutShort->CancelTick, now)
                                                         : now;
        return target;
    }

    // Cyclic content in one sync group continues at the outgoing normalized time, so a
    // walk becoming a run keeps its footfalls.
    bool CarriesPhase(const AnimBoundRig& rig, GameplayTagId fromTag, GameplayTagId toTag)
    {
        const AnimBoundBehavior* from = rig.FindBehavior(fromTag);
        const AnimBoundBehavior* to = rig.FindBehavior(toTag);
        const auto continuous = [](const AnimBoundBehavior& behavior) {
            return behavior.Policy.Kind == AnimBehaviorKind::Cyclic || behavior.Policy.Kind == AnimBehaviorKind::Hold;
        };
        return from != nullptr && to != nullptr
            && rig.ResolveBlend(from->Tag, to->Tag).Phase == AnimPhasePolicy::Carry && to->SyncGroup.IsValid()
            && to->SyncGroup == from->SyncGroup && continuous(*from) && continuous(*to);
    }

    LayerInstanceDecision DecideLayerInstance(const AnimBoundRig& rig, const AnimLayerContent& layer,
                                              const LayerTarget& target, bool lostPin, bool reconstruct)
    {
        using enum LayerInstanceChange;
        if (reconstruct && (target.Driving != nullptr || layer.Request.IsValid()))
            return { Start, AnimChangeReason::Reconstructed };
        if (lostPin)
            return { Start, AnimChangeReason::Rebound };
        if (target.Behavior != layer.Behavior)
            return { CarriesPhase(rig, layer.Behavior, target.Behavior) ? StartCarried : Start,
                     AnimChangeReason::BehaviorChanged };
        if (layer.Pinned && target.Driving != nullptr && target.Driving->Id != layer.Request)
            // Content the superseding request resolves differently starts over from it.
            return { target.Row != layer.Row ? Start : Adopt, AnimChangeReason::RequestSuperseded };
        if (layer.Pinned && target.Driving != nullptr && target.Driving->StartTick != layer.RequestStartTick)
            // Nothing is rewound: the instance restarts where the corrected request puts
            // it, and the pose absorbs the jump like any other change.
            return { Start, AnimChangeReason::RequestCorrected };
        if (target.Row != layer.Row && !layer.Pinned)
            return { ChangeRow, AnimChangeReason::RowChanged };
        return {};
    }

    // A blendspace's phase, or clip time over length.
    float NormalizedTime(const AnimBoundRig& rig, const AnimLayerContent& layer, AnimBehaviorKind kind, AnimTick now,
                         double tickSeconds)
    {
        if (layer.Content < rig.Contents.size() && rig.Contents[layer.Content].Blendspace >= 0)
            return layer.Phase;
        const float length = DurationOf(rig, layer.Content);
        if (length <= 0.0f)
            return 0.0f;
        AnimPlayback playback = layer.Playback;
        playback.DurationSeconds = length;
        playback.Cyclic = IsCyclic(kind);
        return static_cast<float>(AnimPlaybackSeconds(playback, now, tickSeconds) / length);
    }

    LayerEntry EnterLayerInstance(const AnimBoundRig& rig, std::size_t l, AnimLayerContent& layer,
                                  const LayerTarget& target, LayerInstanceDecision decision, AnimTick now,
                                  double tickSeconds, AnimDecisionLog* log)
    {
        LayerEntry entry;
        const auto carryFrom = [&](float normalized) {
            layer.StartTick = now;
            layer.Playback.StartTick = now;
            layer.Playback.OffsetSeconds = normalized * DurationOf(rig, target.Content);
            layer.Carried = true;
            entry.StartNormalized = normalized;
        };
        switch (decision.Change)
        {
        case LayerInstanceChange::Continue:
            break;
        case LayerInstanceChange::Start:
        case LayerInstanceChange::StartCarried:
        {
            const bool carried = decision.Change == LayerInstanceChange::StartCarried;
            const float normalized = carried ? NormalizedTime(rig, layer, KindOf(rig, layer.Behavior), now, tickSeconds)
                                             : 0.0f;
            layer.Behavior = target.Behavior;
            layer.Row = target.Row;
            layer.RowKey = target.RowKey;
            layer.Content = target.Content;
            layer.Request = target.Driving != nullptr ? target.Driving->Id : AnimRequestId{};
            layer.RequestStartTick = target.Driving != nullptr ? target.Driving->StartTick : 0;
            layer.StartTick = target.InstanceStart;
            layer.Playback.StartTick = target.InstanceStart;
            layer.Playback.OffsetSeconds = PlaybackOf(rig, target.Behavior).StartSeconds;
            layer.Carried = false;
            layer.Pinned = target.Kind == AnimBehaviorKind::OneShot || target.Kind == AnimBehaviorKind::Flow;
            entry.StartNormalized = 0.0f;
            entry.Entered = true;
            Log(log, now, l, AnimDecisionCause::ContentChanged, decision.Reason, layer);
            if (carried)
                carryFrom(normalized);
            break;
        }
        case LayerInstanceChange::Adopt:
            layer.Request = target.Driving->Id;
            layer.RequestStartTick = target.Driving->StartTick;
            entry.Adopted = true;
            break;
        case LayerInstanceChange::ChangeRow:
        {
            const float normalized = NormalizedTime(rig, layer, target.Kind, now, tickSeconds);
            layer.Row = target.Row;
            layer.RowKey = target.RowKey;
            layer.Content = target.Content;
            carryFrom(normalized);
            Log(log, now, l, AnimDecisionCause::ContentChanged, decision.Reason, layer);
            break;
        }
        }
        return entry;
    }

    struct LayerStep
    {
        const AnimBoundRig& Rig;
        std::size_t Layer;
        const AnimSelectorState* Selection;
        const AnimPredicateInputs& Inputs;
        const AnimRequest* Driving;
        LayerEntry Entry;
        bool Rebound;
        bool Authority;
        AnimTick Now;
        double TickSeconds;
        AnimDecisionLog* Log;
    };

    void AdvanceFlowLayer(const LayerStep& step, int flowIndex, AnimLayerContent& layer, AnimLayerFlow& flow,
                          LayerRequestProgress& progress)
    {
        const AnimBoundBehavior* bound = step.Rig.FindBehavior(layer.Behavior);
        const AnimBehaviorDecl* policy = bound != nullptr ? &bound->Policy : nullptr;
        // A selector's latch says so; a request-keyed layer cancels when its request was
        // cancelled and the behavior plays a cancel section.
        const bool requestKeyed = step.Rig.Layers[step.Layer].Selector < 0;
        const AnimRequest* driving = step.Driving;
        const bool cancelling = requestKeyed
            ? driving != nullptr && driving->IsCancelled() && policy != nullptr
                && policy->Latch.OnRequestCancel == AnimRequestCancelAction::CancelSection
            : step.Selection != nullptr && step.Selection->Layers[step.Layer].Latch == AnimLatchState::Cancelling;
        const bool abortOnCancel =
            requestKeyed && policy != nullptr && policy->Latch.OnRequestCancel == AnimRequestCancelAction::Abort;

        AnimFlowAdvanceInput tick;
        tick.Rig = &step.Rig;
        tick.Flow = &step.Rig.Flows[static_cast<std::size_t>(flowIndex)];
        tick.Inputs = &step.Inputs;
        tick.Request = driving;
        tick.Now = step.Now;
        tick.TickSeconds = step.TickSeconds;
        tick.Entered = step.Entry.Entered;
        tick.Rebound = step.Rebound;
        tick.Cancelling = cancelling;
        tick.FollowsAnchor = !step.Authority;
        const AnimFlowOutcome outcome =
            AdvanceAnimFlow(tick, layer, flow, static_cast<std::uint8_t>(step.Layer), step.Log);
        layer.ContentComplete = outcome.Complete;

        // The authority anchors the flow's position on its request when it changes or a
        // superseding request takes over; a cancelled request is kept while its flow plays it out.
        if (driving == nullptr)
            return;
        if (outcome.KeepTail && !abortOnCancel)
            progress.Tail = driving->Id;
        if (step.Authority && (outcome.SectionChanged || step.Entry.Adopted))
        {
            progress.Anchored = layer.Request;
            progress.AnchorSection = flow.Section;
            progress.AnchorSectionStartTick = flow.SectionEnteredTick;
        }
    }

    // Phase advances by the tick over the mix's current length, so the samples keep one
    // phase however the weights move.
    void AdvanceBlendspaceLayer(const LayerStep& step, int spaceIndex, std::span<const std::uint32_t> facts,
                                AnimLayerContent& layer)
    {
        const AnimBoundRig& rig = step.Rig;
        const AnimBoundBlendspace& space = rig.Blendspaces[static_cast<std::size_t>(spaceIndex)];
        const AnimBlendspacePoint at = AnimBlendspaceCoordinates(space, facts, rig);
        std::array<float, kAnimBlendspaceMaxSamples> weights{};
        AnimBlendspaceWeights(space, at, weights);
        const float duration = AnimBlendspaceDuration(rig, space, weights);
        const float rate = PlaybackOf(rig, layer.Behavior).Rate;
        const AnimBehaviorKind kind = KindOf(rig, layer.Behavior);
        if (step.Entry.StartNormalized)
            layer.Phase = *step.Entry.StartNormalized;
        else if (duration > 0.0f)
            layer.Phase += static_cast<float>(step.TickSeconds * rate / duration);
        // The phase is a sum of per-tick steps, so the tick that reaches the end may
        // land a rounding short of it; it counts as there.
        constexpr float kEndSlack = 1e-5f;
        if (kind == AnimBehaviorKind::Cyclic)
        {
            layer.Phase = std::max(layer.Phase - std::floor(layer.Phase + kEndSlack), 0.0f);
            layer.ContentComplete = false;
        }
        else
        {
            layer.ContentComplete = rate > 0.0f ? layer.Phase >= 1.0f - kEndSlack
                                  : rate < 0.0f ? layer.Phase <= kEndSlack
                                                : false;
            layer.Phase = layer.ContentComplete ? (rate > 0.0f ? 1.0f : 0.0f) : std::clamp(layer.Phase, 0.0f, 1.0f);
        }
        layer.Coordinates[0] = at[0];
        layer.Coordinates[1] = at[1];
        const int dominant = space.Samples[AnimBlendspaceDominant(space, weights)].Content;
        layer.Clip = dominant >= 0 ? static_cast<std::uint16_t>(dominant) : kAnimNoContent;
        layer.Playback = AnimPlayback{ .StartTick = layer.StartTick, .OffsetSeconds = 0.0f, .Rate = rate,
                                       .DurationSeconds = duration, .Cyclic = IsCyclic(kind) };
        layer.TimeSeconds = layer.Phase * duration;
    }

    void AdvanceClipLayer(const LayerStep& step, AnimLayerContent& layer)
    {
        const AnimBehaviorKind kind = KindOf(step.Rig, layer.Behavior);
        layer.Clip = layer.Content;
        layer.Playback.Rate = PlaybackOf(step.Rig, layer.Behavior).Rate;
        layer.Playback.DurationSeconds = DurationOf(step.Rig, layer.Content);
        layer.Playback.Cyclic = IsCyclic(kind);
        if (layer.Content == kAnimNoContent || layer.Playback.DurationSeconds <= 0.0f)
        {
            // Nothing to play ends at once, so a latch on it cannot hold the layer.
            layer.TimeSeconds = 0.0f;
            layer.ContentComplete = layer.Behavior.IsValid() && kind != AnimBehaviorKind::Cyclic;
            return;
        }
        layer.TimeSeconds = static_cast<float>(AnimPlaybackSeconds(layer.Playback, step.Now, step.TickSeconds));
        layer.ContentComplete = AnimPlaybackEnded(layer.Playback, step.Now, step.TickSeconds);
    }

    // Closed over what a slot row resolves to: a flow, a blendspace or a clip.
    void AdvanceLayerContent(const LayerStep& step, std::span<const std::uint32_t> facts, AnimLayerContent& layer,
                             AnimFlowState* flows, LayerRequestProgress& progress)
    {
        const bool known = layer.Content < step.Rig.Contents.size();
        const int flowIndex = known ? step.Rig.Contents[layer.Content].Flow : -1;
        if (flowIndex >= 0 && flows != nullptr)
        {
            AdvanceFlowLayer(step, flowIndex, layer, flows->Layers[step.Layer], progress);
            return;
        }
        if (flows != nullptr)
            flows->Layers[step.Layer] = AnimLayerFlow{};
        const int spaceIndex = known ? step.Rig.Contents[layer.Content].Blendspace : -1;
        if (spaceIndex >= 0)
            AdvanceBlendspaceLayer(step, spaceIndex, facts, layer);
        else
            AdvanceClipLayer(step, layer);
    }

    // The one place animation writes gameplay's request set.
    void WriteAnimRequestProgress(World& world, EntityId entity, const AnimBoundRig& rig,
                                  const AnimRequestProgress& progress, AnimTick now)
    {
        const bool any = progress.StampTiming
            || std::ranges::any_of(progress.Layers, [](const LayerRequestProgress& layer) {
                   return layer.Tail.IsValid() || layer.Anchored.IsValid();
               });
        AnimRequestSet* set = any ? world.TryGet<AnimRequestSet>(entity) : nullptr;
        if (set == nullptr)
            return;
        if (progress.StampTiming)
            set->RigTiming = rig.TimingIdentity;
        for (const LayerRequestProgress& layer : progress.Layers)
        {
            if (layer.Tail.IsValid())
                ExtendAnimRequestTail(*set, layer.Tail, now + 1);
            if (!layer.Anchored.IsValid())
                continue;
            for (AnimRequest& request : set->Records)
                if (request.Occupied && request.Id == layer.Anchored)
                {
                    request.AnchorSection = layer.AnchorSection;
                    request.AnchorSectionStartTick = layer.AnchorSectionStartTick;
                }
        }
    }

    // Driven by a layer's content, or read by the rule a layer runs.
    bool AnimRequestPlayed(const AnimBoundRig& rig, const AnimSelectorState* selection,
                           const AnimContentState& content, const AnimRequest& request)
    {
        for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
        {
            if (content.Layers[l].Request == request.Id)
                return true;
            const int selector = rig.Layers[l].Selector;
            if (selector < 0 || selection == nullptr || (request.Layers & (1u << l)) == 0)
                continue;
            const std::vector<AnimBoundRule>& rules = rig.Selectors[static_cast<std::size_t>(selector)].Rules;
            const std::uint16_t winner = selection->Layers[l].Winner;
            if (winner >= rules.size())
                continue;
            const AnimBoundRule& rule = rules[winner];
            if (std::ranges::find(rule.Enter.Intents, request.Intent) != rule.Enter.Intents.end()
                || std::ranges::find(rule.Stay.Intents, request.Intent) != rule.Stay.Intents.end())
                return true;
        }
        return false;
    }

    bool Unowned(const World& world, const AnimRequest& request, AnimTick now)
    {
        if (request.Lifetime != AnimRequestLifetime::Held || !IsAnimRequestLive(request, now))
            return false;
        return (request.Id.Source.IsValid() && !world.IsAlive(request.Id.Source))
            || (request.Owner.IsValid() && !world.IsAlive(request.Owner));
    }
}

GameplayTagId AnimLayerBehavior(const AnimBoundRig& rig, std::size_t layer, const AnimSelectorState* selection,
                                const AnimRequestSet* requests, AnimTick now, const AnimLayerContent* playing)
{
    const AnimRequest* driving = rig.Layers[layer].Selector >= 0
                                   ? nullptr
                                   : AnimLayerDrivingRequest(rig, layer, selection, requests, now, playing);
    return LayerBehavior(rig, layer, selection, driving);
}

void ResolveAnimEntity(World& world, EntityId entity, const AnimBoundRig& rig,
                       std::span<const std::uint32_t> facts, const AnimSelectorState* selection,
                       AnimContentState& content, AnimTick now, double tickSeconds, AnimDecisionLog* log)
{
    const World& reader = world;
    const AnimRequestSet* requests =
        reader.IsRegistered<AnimRequestSet>() ? reader.TryGet<AnimRequestSet>(entity) : nullptr;
    AnimFlowState* flows = world.IsRegistered<AnimFlowState>() ? world.TryGet<AnimFlowState>(entity) : nullptr;
    const bool authority = IsSimulationAuthority(world);

    AnimPredicateInputs inputs;
    inputs.Facts = facts;
    inputs.Tags = reader.IsRegistered<GameplayTagContainer>() ? reader.TryGet<GameplayTagContainer>(entity) : nullptr;
    inputs.Registry = reader.TryGetResource<GameplayTagRegistry>();
    inputs.Requests = requests;
    inputs.Now = now;
    inputs.TickSeconds = tickSeconds;

    const bool rebound = content.BindingGeneration != rig.Generation;
    content.BindingGeneration = rig.Generation;

    AnimRequestProgress progress;
    if (requests != nullptr)
        NoteRigTiming(rig, *requests, authority, content, progress, now, log);

    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
    {
        AnimLayerContent& layer = content.Layers[l];
        inputs.LayerBit = static_cast<std::uint8_t>(1u << l);
        inputs.BehaviorStartTick = selection != nullptr ? selection->Layers[l].WinnerStartTick : layer.StartTick;

        const bool lostPin = rebound && RemapLayerAfterRebind(rig, l, layer, now, log);
        const LayerTarget target = ResolveLayerTarget(rig, l, selection, requests, inputs, layer, now);
        const LayerInstanceDecision decision = DecideLayerInstance(rig, layer, target, lostPin, content.Reconstruct);
        const LayerStep step{ .Rig = rig,
                              .Layer = l,
                              .Selection = selection,
                              .Inputs = inputs,
                              .Driving = target.Driving,
                              .Entry = EnterLayerInstance(rig, l, layer, target, decision, now, tickSeconds, log),
                              .Rebound = rebound,
                              .Authority = authority,
                              .Now = now,
                              .TickSeconds = tickSeconds,
                              .Log = log };
        AdvanceLayerContent(step, facts, layer, flows, progress.Layers[l]);
    }
    WriteAnimRequestProgress(world, entity, rig, progress, now);
    content.Reconstruct = false;
}

bool NoteAnimRequestOutcomes(const World& world, EntityId entity, const AnimBoundRig& rig,
                             const AnimSelectorState* selection, const AnimContentState& content, AnimTick now,
                             AnimRequestReport& report, AnimDecisionLog* log)
{
    const AnimRequestSet* requests =
        world.IsRegistered<AnimRequestSet>() ? world.TryGet<AnimRequestSet>(entity) : nullptr;
    if (requests == nullptr)
        return false;
    const bool authority = IsSimulationAuthority(world);
    bool orphaned = false;
    for (std::size_t slot = 0; slot < kAnimRequestCapacity; ++slot)
    {
        const AnimRequest& request = requests->Records[slot];
        const std::uint32_t sequence = IsAnimRequestRetained(request, now) ? request.Id.Sequence : 0;
        const auto bit = static_cast<std::uint8_t>(1u << slot);
        const auto clear = static_cast<std::uint8_t>(~bit);
        if (report.Seen[slot] != sequence)
        {
            if (report.Seen[slot] != 0 && (report.Played & bit) == 0)
                ++report.Unplayed;
            report.Seen[slot] = sequence;
            report.Played &= clear;
            report.Unowned &= clear;
            report.Reported &= clear;
        }
        if (sequence == 0)
            continue;
        if ((report.Played & bit) == 0 && AnimRequestPlayed(rig, selection, content, request))
            report.Played |= bit;

        // Held requests end only when their producer says so; one that outlived its
        // producer is that producer's bug, made visible rather than tidied away.
        if (!authority || (report.Reported & bit) != 0 || !Unowned(world, request, now))
            continue;
        if ((report.Unowned & bit) == 0)
        {
            report.Unowned |= bit;
            continue;
        }
        report.Reported |= bit;
        ++report.Orphaned;
        orphaned = true;
        if (log != nullptr)
        {
            AnimDecisionRecord record;
            record.Tick = now;
            record.Cause = AnimDecisionCause::RequestOrphaned;
            record.Request = request.Id;
            record.Intent = request.Intent;
            log->Append(record);
        }
    }
    return orphaned;
}

void AnimContentSystem::FixedLogic(FixedLogicContext& ctx)
{
    ResolveImpl(ctx.Entities, &ctx.Partitions, AnimClockAt(ctx.Entities, ctx.Time.TickIndex), ctx.Time.DeltaSeconds);
}

void AnimContentSystem::Resolve(World& world, AnimTick now, double tickSeconds)
{
    ResolveImpl(world, nullptr, AnimClock::Uniform(now), tickSeconds);
}

void AnimContentSystem::ResolveImpl(World& world, const StoragePartitionSet* partitions, const AnimClock& clock,
                                    double tickSeconds)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>() || !world.IsRegistered<AnimContentState>()
        || !world.IsRegistered<AnimRequestReport>())
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

    AnimRigRunCache resolver(*bindings, world);
    const auto visit = [&](auto& view) {
        const auto rigs = view.template Read<AnimRig>();
        auto contents = view.template Write<AnimContentState>();
        auto reports = view.template Write<AnimRequestReport>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = resolver.Resolve(rigs[i].Rig);
            if (rig == nullptr || !rig->Valid || !ShouldRunAnimationLogic(PresentsPose, *rig))
                continue;
            const EntityId entity = view.Entity(i);
            // Facts and selection are optional: a Prop carries neither.
            std::span<const std::uint32_t> facts;
            const World& reader = world;
            if (const AnimFacts* small = hasSmall ? reader.TryGet<AnimFacts>(entity) : nullptr)
                facts = std::span<const std::uint32_t>(small->Values, std::min(rig->Slots.size(), kAnimFactsSmall));
            else if (const AnimFactsLarge* large = hasLarge ? reader.TryGet<AnimFactsLarge>(entity) : nullptr)
                facts = std::span<const std::uint32_t>(large->Values, std::min(rig->Slots.size(), kAnimFactsLarge));
            const AnimSelectorState* selection = hasSelection ? reader.TryGet<AnimSelectorState>(entity) : nullptr;
            AnimDecisionLog* log = hasLog ? world.TryGet<AnimDecisionLog>(entity) : nullptr;
            const AnimTick now = clock.For(entity);
            ResolveAnimEntity(world, entity, *rig, facts, selection, contents[i], now, tickSeconds, log);
            if (NoteAnimRequestOutcomes(world, entity, *rig, selection, contents[i], now, reports[i], log)
                && Logging != nullptr)
                Logging->GetLogger<AnimContentSystem>().Warn(
                    "'{}' holds an animation request whose producer ended without cancelling it; "
                    "the producer owns a held request's lifetime (anim.risk counts them).",
                    rig->RigPath);
        }
    };
    if (partitions != nullptr)
        ContentQuery->ForEachChunkIn(*partitions, visit);
    else
        ContentQuery->ForEachChunk(visit);
}
