#include <anim/AnimContentSystem.h>

#include <anim/AnimBlendspace.h>
#include <anim/AnimBlendspaceData.h>
#include <anim/AnimFacts.h>
#include <anim/AnimFlowRunner.h>
#include <anim/AnimRequests.h>
#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

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

    // A behavior the rig does not know plays as authored.
    const AnimBehaviorDecl& PlaybackOf(const AnimBoundRig& rig, GameplayTagId behavior)
    {
        static const AnimBehaviorDecl asAuthored;
        const AnimBoundBehavior* bound = rig.FindBehavior(behavior);
        return bound != nullptr ? bound->Policy : asAuthored;
    }

    // Wrapped either way round for cyclic content; held at its ends otherwise.
    double PlacedTime(double elapsed, double duration, bool cyclic)
    {
        if (!cyclic)
            return std::clamp(elapsed, 0.0, duration);
        const double wrapped = std::fmod(elapsed, duration);
        return wrapped < 0.0 ? wrapped + duration : wrapped;
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

GameplayTagId AnimLayerBehavior(const AnimBoundRig& rig, std::size_t layer, const AnimSelectorState* selection,
                                const AnimRequestSet* requests, AnimTick now, const AnimLayerContent* playing)
{
    const AnimBoundLayer& bound = rig.Layers[layer];
    if (bound.Selector >= 0)
    {
        if (selection != nullptr && selection->Layers[layer].Winner != kAnimNoRule)
            return selection->Layers[layer].Behavior;
        return bound.Idle;
    }
    const AnimRequest* request = AnimLayerDrivingRequest(rig, layer, selection, requests, now, playing);
    return request != nullptr ? request->Intent : bound.Idle;
}

const AnimRequest* AnimLayerDrivingRequest(const AnimBoundRig& rig, std::size_t layer,
                                           const AnimSelectorState* selection, const AnimRequestSet* requests,
                                           AnimTick now, const AnimLayerContent* playing)
{
    if (requests == nullptr)
        return nullptr;
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << layer);
    const auto newer = [](const AnimRequest& a, const AnimRequest* b) {
        return b == nullptr || a.StartTick > b->StartTick
            || (a.StartTick == b->StartTick && a.Id.Sequence > b->Id.Sequence);
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

    void NoteAnimRequestsPlayed(const AnimBoundRig& rig, const AnimSelectorState* selection,
                                const AnimRequestSet& requests, AnimTick now, AnimContentState& content)
    {
        for (std::size_t slot = 0; slot < kAnimRequestCapacity; ++slot)
        {
            const AnimRequest& request = requests.Records[slot];
            const std::uint32_t sequence = IsAnimRequestRetained(request, now) ? request.Id.Sequence : 0;
            const auto bit = static_cast<std::uint8_t>(1u << slot);
            if (content.RequestSeen[slot] != sequence)
            {
                if (content.RequestSeen[slot] != 0 && (content.RequestPlayed & bit) == 0)
                    ++content.UnplayedRequests;
                content.RequestSeen[slot] = sequence;
                content.RequestPlayed = static_cast<std::uint8_t>(content.RequestPlayed & ~bit);
            }
            if (sequence != 0 && (content.RequestPlayed & bit) == 0
                && AnimRequestPlayed(rig, selection, content, request))
                content.RequestPlayed = static_cast<std::uint8_t>(content.RequestPlayed | bit);
        }
    }
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

    // The authority says which timing its requests were made under; a machine
    // binding the rig differently cannot reconstruct them, and says so.
    if (requests != nullptr)
    {
        if (authority)
        {
            if (requests->RigTiming != rig.TimingIdentity)
                world.TryGet<AnimRequestSet>(entity)->RigTiming = rig.TimingIdentity;
        }
        else
        {
            const bool disagrees = requests->RigTiming != 0 && requests->RigTiming != rig.TimingIdentity;
            if (disagrees != content.TimingDisagrees)
            {
                content.TimingDisagrees = disagrees;
                if (log != nullptr)
                {
                    AnimDecisionRecord record;
                    record.Tick = now;
                    record.Cause = disagrees ? AnimDecisionCause::TimingDisagreed : AnimDecisionCause::TimingAgreed;
                    log->Append(record);
                }
            }
        }
    }

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
                    Log(log, now, l, AnimDecisionCause::IndexReset, AnimChangeReason::Rebound, layer);
            }
            else
            {
                layer.Content = static_cast<std::uint16_t>(rig.SlotRows[remapped].Content);
            }
            layer.Row = remapped;
        }

        const AnimRequest* driving = AnimLayerDrivingRequest(rig, l, selection, requests, now, &layer);
        const GameplayTagId behavior = AnimLayerBehavior(rig, l, selection, requests, now, &layer);
        const int row = ResolveAnimSlotRow(rig, behavior, inputs);
        const std::uint16_t resolvedRow = row >= 0 ? static_cast<std::uint16_t>(row) : kAnimNoContent;
        const std::uint16_t resolvedContent =
            row >= 0 ? static_cast<std::uint16_t>(rig.SlotRows[static_cast<std::size_t>(row)].Content) : kAnimNoContent;
        const AnimBehaviorKind kind = KindOf(rig, behavior);

        // Content time starts where every machine can place it: the driving
        // request's start, or the cancel that cut the last content short;
        // otherwise this tick. See docs/gameplay/animation.md.
        const AnimRequest* cutShort =
            driving == nullptr && !layer.ContentComplete ? CancelledAnimRequest(requests, layer.Request) : nullptr;
        const AnimTick instanceStart = driving != nullptr   ? std::min(driving->StartTick, now)
                                     : cutShort != nullptr ? std::min(cutShort->CancelTick, now)
                                                           : now;
        // The outgoing content's normalized time: a blendspace's phase or clip time over length.
        const auto outgoingNormalized = [&](AnimBehaviorKind outgoingKind) {
            if (layer.Content < rig.Contents.size() && rig.Contents[layer.Content].Blendspace >= 0)
                return layer.Phase;
            const float length = DurationOf(rig, layer.Content);
            if (length <= 0.0f)
                return 0.0f;
            const double elapsed = static_cast<double>(layer.StartOffsetSeconds)
                + static_cast<double>(now >= layer.StartTick ? now - layer.StartTick : 0) * tickSeconds
                    * static_cast<double>(layer.ClipRate);
            const double at = PlacedTime(elapsed, length, outgoingKind == AnimBehaviorKind::Cyclic);
            return static_cast<float>(at / length);
        };
        // Set when content starts this tick: the normalized time it starts at.
        std::optional<float> startNormalized;
        const auto startInstance = [&](AnimChangeReason reason) {
            layer.Behavior = behavior;
            layer.Row = resolvedRow;
            layer.RowKey = row >= 0 ? rig.SlotRows[static_cast<std::size_t>(row)].Key : 0;
            layer.Content = resolvedContent;
            layer.Request = driving != nullptr ? driving->Id : AnimRequestId{};
            layer.RequestStartTick = driving != nullptr ? driving->StartTick : 0;
            layer.StartTick = instanceStart;
            layer.StartOffsetSeconds = PlaybackOf(rig, behavior).StartSeconds;
            layer.Pinned = kind == AnimBehaviorKind::OneShot || kind == AnimBehaviorKind::Flow;
            startNormalized = 0.0f;
            Log(log, now, l, AnimDecisionCause::ContentChanged, reason, layer);
        };

        bool entered = false;
        bool adopted = false;
        if (content.Reconstruct && (driving != nullptr || layer.Request.IsValid()))
        {
            startInstance(AnimChangeReason::Reconstructed);
            entered = true;
        }
        else if (behavior != layer.Behavior || lostPin)
        {
            // Cyclic content in one sync group continues at the outgoing normalized time, so a
            // walk becoming a run keeps its footfalls.
            const AnimBoundBehavior* from = rig.FindBehavior(layer.Behavior);
            const AnimBoundBehavior* to = rig.FindBehavior(behavior);
            const bool carry = !lostPin && from != nullptr && to != nullptr
                && rig.ResolveBlend(from->Tag, to->Tag).Phase == AnimPhasePolicy::Carry && to->SyncGroup.IsValid()
                && to->SyncGroup == from->SyncGroup
                && (from->Policy.Kind == AnimBehaviorKind::Cyclic || from->Policy.Kind == AnimBehaviorKind::Hold)
                && (to->Policy.Kind == AnimBehaviorKind::Cyclic || to->Policy.Kind == AnimBehaviorKind::Hold);
            const float normalized = carry ? outgoingNormalized(from->Policy.Kind) : 0.0f;
            startInstance(lostPin ? AnimChangeReason::Rebound : AnimChangeReason::BehaviorChanged);
            if (carry)
            {
                // From this tick, since the carried phase was reached now.
                layer.StartTick = now;
                layer.StartOffsetSeconds = normalized * DurationOf(rig, resolvedContent);
                startNormalized = normalized;
            }
            entered = true;
        }
        else if (layer.Pinned && driving != nullptr && driving->Id != layer.Request)
        {
            // A superseding request: content it resolves differently starts
            // over from the new request; the same content runs on under it.
            if (resolvedRow != layer.Row)
            {
                startInstance(AnimChangeReason::RequestSuperseded);
                entered = true;
            }
            else
            {
                layer.Request = driving->Id;
                layer.RequestStartTick = driving->StartTick;
                adopted = true;
            }
        }
        else if (layer.Pinned && driving != nullptr && driving->Id == layer.Request
                 && driving->StartTick != layer.RequestStartTick)
        {
            // Nothing is rewound: the instance restarts where the corrected request puts it,
            // and the pose absorbs the jump like any other change.
            startInstance(AnimChangeReason::RequestCorrected);
            entered = true;
        }
        else if (resolvedRow != layer.Row && !layer.Pinned)
        {
            // Cyclic and hold content takes a new row now, at the same normalized time.
            const float normalized = outgoingNormalized(kind);
            layer.Row = resolvedRow;
            layer.RowKey = row >= 0 ? rig.SlotRows[static_cast<std::size_t>(row)].Key : 0;
            layer.Content = resolvedContent;
            layer.StartTick = now;
            layer.StartOffsetSeconds = normalized * DurationOf(rig, resolvedContent);
            startNormalized = normalized;
            Log(log, now, l, AnimDecisionCause::ContentChanged, AnimChangeReason::RowChanged, layer);
        }

        const int flowIndex = layer.Content < rig.Contents.size() ? rig.Contents[layer.Content].Flow : -1;
        if (flowIndex >= 0 && flows != nullptr)
        {
            const AnimBoundBehavior* bound = rig.FindBehavior(layer.Behavior);
            const AnimBehaviorDecl* policy = bound != nullptr ? &bound->Policy : nullptr;
            // A selector's latch says so; a request-keyed layer cancels when its request was
            // cancelled and the behavior plays a cancel section.
            const bool requestKeyed = rig.Layers[l].Selector < 0;
            const bool cancelling = requestKeyed
                ? driving != nullptr && driving->IsCancelled() && policy != nullptr
                    && policy->Latch.OnRequestCancel == AnimRequestCancelAction::CancelSection
                : selection != nullptr && selection->Layers[l].Latch == AnimLatchState::Cancelling;
            const bool abortOnCancel = requestKeyed && policy != nullptr
                && policy->Latch.OnRequestCancel == AnimRequestCancelAction::Abort;

            AnimFlowAdvanceInput tick;
            tick.Rig = &rig;
            tick.Flow = &rig.Flows[static_cast<std::size_t>(flowIndex)];
            tick.Inputs = &inputs;
            tick.Request = driving;
            tick.Now = now;
            tick.TickSeconds = tickSeconds;
            tick.Entered = entered;
            tick.Cancelling = cancelling;
            tick.FollowsAnchor = !authority;
            const AnimFlowOutcome outcome =
                AdvanceAnimFlow(tick, layer, flows->Layers[l], static_cast<std::uint8_t>(l), log);
            layer.ContentComplete = outcome.Complete;

            // The authority stamps the flow's position on its request when it changes or a
            // superseding request takes over, and keeps a cancelled request while its flow plays it out.
            const bool stamp = authority && (outcome.SectionChanged || adopted) && driving != nullptr;
            const bool keepTail = outcome.KeepTail && !abortOnCancel && driving != nullptr;
            if (stamp || keepTail)
            {
                if (AnimRequestSet* writable = world.TryGet<AnimRequestSet>(entity))
                {
                    if (keepTail)
                        ExtendAnimRequestTail(*writable, driving->Id, now + 1);
                    if (stamp)
                        for (AnimRequest& request : writable->Records)
                            if (request.Occupied && request.Id == layer.Request)
                            {
                                request.AnchorSection = flows->Layers[l].Section;
                                request.AnchorSectionStartTick = flows->Layers[l].SectionStartTick;
                            }
                }
            }
            continue;
        }

        if (flows != nullptr)
            flows->Layers[l] = AnimLayerFlow{};

        const int spaceIndex = layer.Content < rig.Contents.size() ? rig.Contents[layer.Content].Blendspace : -1;
        if (spaceIndex >= 0)
        {
            // Phase advances by the tick over the mix's current length, so the samples keep one
            // phase however the weights move.
            const AnimBoundBlendspace& space = rig.Blendspaces[static_cast<std::size_t>(spaceIndex)];
            const AnimBlendspacePoint at = AnimBlendspaceCoordinates(space, facts, rig);
            std::array<float, kAnimBlendspaceMaxSamples> weights{};
            AnimBlendspaceWeights(space, at, weights);
            const float duration = AnimBlendspaceDuration(rig, space, weights);
            const float rate = PlaybackOf(rig, layer.Behavior).Rate;
            if (startNormalized)
                layer.Phase = *startNormalized;
            else if (duration > 0.0f)
                layer.Phase += static_cast<float>(tickSeconds * rate / duration);
            // The phase is a sum of per-tick steps, so the tick that reaches
            // the end may land a rounding short of it; it counts as there.
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
            layer.ClipStartTick = layer.StartTick;
            layer.ClipOffsetSeconds = 0.0f;
            layer.ClipRate = rate;
            layer.TimeSeconds = layer.Phase * duration;
            continue;
        }
        const float rate = PlaybackOf(rig, layer.Behavior).Rate;
        layer.Clip = layer.Content;
        layer.ClipStartTick = layer.StartTick;
        layer.ClipOffsetSeconds = layer.StartOffsetSeconds;
        layer.ClipRate = rate;
        const float duration = DurationOf(rig, layer.Content);
        const double elapsed = static_cast<double>(layer.StartOffsetSeconds)
            + static_cast<double>(now >= layer.StartTick ? now - layer.StartTick : 0) * tickSeconds
                * static_cast<double>(rate);
        if (layer.Content == kAnimNoContent || duration <= 0.0f)
        {
            layer.TimeSeconds = 0.0f;
            layer.ContentComplete = layer.Content != kAnimNoContent && kind != AnimBehaviorKind::Cyclic;
        }
        else
        {
            const bool cyclic = kind == AnimBehaviorKind::Cyclic;
            layer.TimeSeconds = static_cast<float>(PlacedTime(elapsed, duration, cyclic));
            // Played forward it ends at its length, backward at its start;
            // held, it does not end.
            layer.ContentComplete = !cyclic
                && (rate > 0.0f ? elapsed >= static_cast<double>(duration) : rate < 0.0f && elapsed <= 0.0);
        }
    }
    if (requests != nullptr)
        NoteAnimRequestsPlayed(rig, selection, *requests, now, content);
    content.Reconstruct = false;
}

void AnimContentSystem::FixedLogic(FixedLogicContext& ctx)
{
    ResolveImpl(ctx.Entities, &ctx.Partitions, AuthorityTickOf(ctx.Entities, ctx.Time.TickIndex), ctx.Time.DeltaSeconds);
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
            const World& reader = world;
            if (const AnimFacts* small = hasSmall ? reader.TryGet<AnimFacts>(entity) : nullptr)
                facts = std::span<const std::uint32_t>(small->Values, std::min(rig->Slots.size(), kAnimFactsSmall));
            else if (const AnimFactsLarge* large = hasLarge ? reader.TryGet<AnimFactsLarge>(entity) : nullptr)
                facts = std::span<const std::uint32_t>(large->Values, std::min(rig->Slots.size(), kAnimFactsLarge));
            const AnimSelectorState* selection = hasSelection ? reader.TryGet<AnimSelectorState>(entity) : nullptr;
            AnimDecisionLog* log = hasLog ? world.TryGet<AnimDecisionLog>(entity) : nullptr;
            ResolveAnimEntity(world, entity, *rig, facts, selection, contents[i], now, tickSeconds, log);
        }
    };
    if (partitions != nullptr)
        ContentQuery->ForEachChunkIn(*partitions, visit);
    else
        ContentQuery->ForEachChunk(visit);
}
