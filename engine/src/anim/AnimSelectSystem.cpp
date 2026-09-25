#include <anim/AnimSelectSystem.h>

#include <anim/AnimRequests.h>
#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/SimulationAuthority.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    void Mix(std::uint64_t& hash, std::uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
        {
            hash ^= (value >> (i * 8)) & 0xFFu;
            hash *= kFnvPrime;
        }
    }

    std::uint64_t RequestDigest(const AnimRequestSet* set, AnimTick now)
    {
        std::uint64_t hash = kFnvOffset;
        if (set == nullptr)
            return hash;
        for (const AnimRequest& request : set->Records)
        {
            if (!request.Occupied)
                continue;
            Mix(hash, request.Id.Sequence);
            Mix(hash, request.Intent.Value);
            Mix(hash, request.StartTick);
            Mix(hash, request.CancelTick);
            Mix(hash, request.TailUntilTick);
            Mix(hash, request.FixedTicks);
            Mix(hash, (static_cast<std::uint64_t>(request.Layers) << 16)
                          | (static_cast<std::uint64_t>(request.Lifetime) << 8)
                          | static_cast<std::uint64_t>(request.CancelReason));
            for (const std::uint32_t param : request.Params)
                Mix(hash, param);
            // Liveness moves with time alone for fixed and impulse requests.
            Mix(hash, IsAnimRequestLive(request, now) ? 1u : 0u);
        }
        return hash;
    }

    std::uint64_t FactDigest(std::span<const std::uint32_t> facts, const AnimContentState* content)
    {
        std::uint64_t hash = kFnvOffset;
        for (const std::uint32_t value : facts)
            Mix(hash, value);
        if (content != nullptr)
        {
            std::uint64_t feedback = 0;
            for (std::size_t l = 0; l < kAnimMaxLayers; ++l)
                feedback |= static_cast<std::uint64_t>(content->Layers[l].ContentComplete) << l;
            Mix(hash, feedback);
        }
        return hash;
    }

    bool Interrupts(const AnimBoundRule& rule, const AnimBoundBehavior& latched,
                    const GameplayTagRegistry* registry)
    {
        switch (latched.Policy.Latch.InterruptibleBy)
        {
        case AnimInterruptKind::PriorityAtLeast:
            return rule.Band >= latched.Policy.Latch.Priority;
        case AnimInterruptKind::Tags:
            return std::any_of(latched.InterruptTags.begin(), latched.InterruptTags.end(), [&](GameplayTagId tag) {
                return rule.Behavior == tag || (registry != nullptr && registry->IsDescendantOf(rule.Behavior, tag));
            });
        case AnimInterruptKind::Never:
            return false;
        }
        return false;
    }

    const AnimBoundBehavior* BehaviorOf(const AnimBoundRig& rig, const AnimBoundRule& rule)
    {
        return rule.BehaviorIndex >= 0 ? &rig.Behaviors[static_cast<std::size_t>(rule.BehaviorIndex)] : nullptr;
    }
}

std::string_view AnimRuleVerdictName(AnimRuleVerdictKind kind)
{
    switch (kind)
    {
    case AnimRuleVerdictKind::NotEvaluated: return "not evaluated";
    case AnimRuleVerdictKind::Winner: return "winner";
    case AnimRuleVerdictKind::Passed: return "passed, lower priority";
    case AnimRuleVerdictKind::Failed: return "false operand";
    case AnimRuleVerdictKind::Cooldown: return "cooldown";
    case AnimRuleVerdictKind::BlockedByLatch: return "blocked by latch";
    case AnimRuleVerdictKind::BlockedByHold: return "blocked by hold";
    }
    return "unknown";
}

AnimTick AnimMsToTicks(float ms, double tickSeconds)
{
    if (ms <= 0.0f || tickSeconds <= 0.0)
        return 0;
    return static_cast<AnimTick>(std::ceil(static_cast<double>(ms) / (tickSeconds * 1000.0) - 1e-9));
}

AnimSelectionOutcome SelectAnimLayer(const AnimBoundRig& rig, const AnimBoundSelector& selector,
                                     AnimLayerSelection& state, AnimLayerSelectInputs inputs,
                                     std::span<AnimRuleVerdict> verdicts)
{
    const std::vector<AnimBoundRule>& rules = selector.Rules;
    const std::size_t count = rules.size();
    const AnimTick now = inputs.Predicate.Now;
    const double tick = inputs.Predicate.TickSeconds;
    inputs.Predicate.BehaviorStartTick = state.WinnerStartTick;

    const auto verdict = [&](std::size_t rule) -> AnimRuleVerdict* {
        return rule < verdicts.size() ? &verdicts[rule] : nullptr;
    };
    for (AnimRuleVerdict& v : verdicts)
        v = AnimRuleVerdict{};

    const auto enters = [&](std::size_t i) {
        const AnimBoundRule& rule = rules[i];
        if (rule.CooldownSlot >= 0 && now < state.CooldownUntil[rule.CooldownSlot])
        {
            if (AnimRuleVerdict* v = verdict(i))
                v->Kind = AnimRuleVerdictKind::Cooldown;
            return false;
        }
        const AnimPredicateResult result = EvaluateAnimProgram(rule.Enter, inputs.Predicate);
        if (AnimRuleVerdict* v = verdict(i))
        {
            v->Kind = result.Passed ? AnimRuleVerdictKind::Passed : AnimRuleVerdictKind::Failed;
            v->Evaluation = result;
        }
        return result.Passed;
    };
    // The first rule in [begin, end) that enters, skipping those `blocked`
    // keeps out with the verdict it names.
    const auto firstEntering = [&](std::size_t begin, std::size_t end, auto&& blocked) -> std::size_t {
        for (std::size_t i = begin; i < end; ++i)
        {
            if (const std::optional<AnimRuleVerdictKind> reason = blocked(i))
            {
                if (AnimRuleVerdict* v = verdict(i))
                    v->Kind = *reason;
                continue;
            }
            if (enters(i))
                return i;
        }
        return count;
    };
    const auto unblocked = [](std::size_t) -> std::optional<AnimRuleVerdictKind> { return std::nullopt; };

    AnimSelectionOutcome outcome;
    const std::size_t current = state.Winner < count ? state.Winner : count;
    std::size_t next = current;

    // The latch first: it may end this tick, and while it holds it is the
    // winner's stay.
    const AnimBoundBehavior* latched =
        current < count && state.Latch != AnimLatchState::None ? BehaviorOf(rig, rules[current]) : nullptr;
    if (latched != nullptr)
    {
        const AnimLatchPolicy& latch = latched->Policy.Latch;
        if (state.Latch == AnimLatchState::Held && latch.Mode == AnimLatchMode::UntilRequestEnds)
        {
            bool live = false;
            if (inputs.Predicate.Requests != nullptr)
            {
                for (const AnimRequest& request : inputs.Predicate.Requests->Records)
                {
                    if (!IsAnimRequestLive(request, now))
                        continue;
                    if (request.Id == state.LatchRequest)
                    {
                        live = true;
                        break;
                    }
                    // Superseded in place: the source's newer request for the
                    // same intent carries the latch on, which is how a combo
                    // advances without the latch letting go.
                    if (request.Id.Source == state.LatchRequest.Source
                        && request.Id.Sequence > state.LatchRequest.Sequence
                        && request.Intent == rules[current].LatchIntent)
                    {
                        state.LatchRequest = request.Id;
                        live = true;
                        break;
                    }
                }
            }
            if (!live)
                state.Latch = latch.OnRequestCancel == AnimRequestCancelAction::Abort   ? AnimLatchState::None
                    : latch.OnRequestCancel == AnimRequestCancelAction::CancelSection ? AnimLatchState::Cancelling
                                                                                      : AnimLatchState::Finishing;
        }
        if (state.Latch != AnimLatchState::None
            && (state.Latch == AnimLatchState::Finishing || state.Latch == AnimLatchState::Cancelling
                || latch.Mode == AnimLatchMode::UntilComplete)
            && inputs.ContentComplete)
            state.Latch = AnimLatchState::None;
        if (state.Latch == AnimLatchState::None)
        {
            outcome.LatchReleased = true;
            latched = nullptr;
        }
    }

    const bool holdExpiredNow = state.HoldUntilTick != 0 && state.HoldUntilTick == now;
    if (latched != nullptr)
    {
        // Only rules above the winner are tried, and only those the latch
        // lets through may take over; the rest are recorded as blocked.
        next = current;
        // A latch already cancelling has committed to its cancel section: it
        // is not interrupted again until that section completes.
        const bool cancelling = state.Latch == AnimLatchState::Cancelling;
        for (std::size_t i = 0; i < current; ++i)
        {
            if (!enters(i))
                continue;
            if (!cancelling && Interrupts(rules[i], *latched, inputs.Predicate.Registry))
            {
                next = i;
                break;
            }
            if (AnimRuleVerdict* v = verdict(i))
                v->Kind = AnimRuleVerdictKind::BlockedByLatch;
        }
        if (next != current)
        {
            outcome.LatchInterrupted = true;
            // Interrupted with a cancel-section policy, the latch keeps the
            // layer while its flow plays the cancel section, and the rule that
            // interrupted takes over when that completes.
            if (latched->Policy.Latch.OnInterrupt == AnimInterruptAction::CancelSection)
            {
                state.Latch = AnimLatchState::Cancelling;
                next = current;
            }
        }
        else if (AnimRuleVerdict* v = verdict(current))
            *v = AnimRuleVerdict{ .Kind = AnimRuleVerdictKind::Winner, .Stayed = true, .EvaluatedStay = false, .Evaluation = {} };
    }
    else if (current < count)
    {
        const AnimPredicateResult stay = EvaluateAnimProgram(rules[current].Stay, inputs.Predicate);
        const bool holding = now < state.HoldUntilTick;
        const auto heldOut = [&](std::size_t i) -> std::optional<AnimRuleVerdictKind> {
            if (holding && rules[i].Band <= rules[current].Band)
                return AnimRuleVerdictKind::BlockedByHold;
            return std::nullopt;
        };
        if (stay.Passed || holding)
        {
            next = firstEntering(0, current, heldOut);
            if (next == count)
                next = current;
            if (next == current)
            {
                if (AnimRuleVerdict* v = verdict(current))
                    *v = AnimRuleVerdict{ .Kind = AnimRuleVerdictKind::Winner, .Stayed = true, .EvaluatedStay = true, .Evaluation = stay };
                if (!stay.Passed)
                {
                    // Held despite a failing stay: the rules below were kept
                    // out by the hold, not by ranking.
                    for (std::size_t i = current + 1; i < count; ++i)
                        if (AnimRuleVerdict* v = verdict(i))
                            v->Kind = AnimRuleVerdictKind::BlockedByHold;
                }
            }
        }
        else
        {
            next = firstEntering(0, count, unblocked);
            if (next != current)
            {
                if (AnimRuleVerdict* v = verdict(current))
                {
                    v->Kind = AnimRuleVerdictKind::Failed;
                    v->Evaluation = stay;
                    v->EvaluatedStay = true;
                }
            }
        }
    }
    else
    {
        next = firstEntering(0, count, unblocked);
    }

    if (next < count)
    {
        if (AnimRuleVerdict* v = verdict(next))
        {
            if (v->Kind != AnimRuleVerdictKind::Winner)
                v->Kind = AnimRuleVerdictKind::Winner;
        }
        // Rules below a winner that entered this tick were never reached.
        if (next != current)
            for (std::size_t i = next + 1; i < count; ++i)
                if (AnimRuleVerdict* v = verdict(i); v != nullptr && v->Kind == AnimRuleVerdictKind::Passed)
                    v->Kind = AnimRuleVerdictKind::NotEvaluated;
    }

    if (next != current)
    {
        outcome.Changed = true;
        outcome.Previous = current < count ? static_cast<std::uint16_t>(current) : kAnimNoRule;
        if (current < count && rules[current].CooldownSlot >= 0)
            state.CooldownUntil[rules[current].CooldownSlot] = now + AnimMsToTicks(rules[current].CooldownMs, tick);

        outcome.Reason = outcome.LatchInterrupted ? AnimChangeReason::LatchInterrupted
            : outcome.LatchReleased             ? AnimChangeReason::LatchComplete
            : holdExpiredNow                    ? AnimChangeReason::HoldExpired
            : inputs.RequestsChanged            ? AnimChangeReason::RequestsChanged
                                                : AnimChangeReason::FactsChanged;

        state.Previous = outcome.Previous;
        state.Latch = AnimLatchState::None;
        state.LatchRequest = {};
        if (next < count)
        {
            const AnimBoundRule& rule = rules[next];
            state.Winner = static_cast<std::uint16_t>(next);
            state.WinnerKey = rule.Key;
            state.Behavior = rule.Behavior;
            state.WinnerStartTick = now;
            state.HoldUntilTick = rule.HoldMinMs > 0.0f ? now + AnimMsToTicks(rule.HoldMinMs, tick) : 0;
            const AnimBoundBehavior* behavior = BehaviorOf(rig, rule);
            if (behavior != nullptr && behavior->Policy.Latch.Mode != AnimLatchMode::None)
            {
                state.Latch = AnimLatchState::Held;
                state.LatchStartTick = now;
                if (rule.LatchIntent.IsValid() && inputs.Predicate.Requests != nullptr)
                {
                    if (const AnimRequest* request = FindPrimaryAnimRequest(
                            *inputs.Predicate.Requests, rule.LatchIntent, now, inputs.Predicate.LayerBit))
                        state.LatchRequest = request->Id;
                }
                outcome.LatchArmed = true;
            }
        }
        else
        {
            state.Winner = kAnimNoRule;
            state.WinnerKey = 0;
            state.Behavior = {};
            state.WinnerStartTick = now;
            state.HoldUntilTick = 0;
        }
    }

    // Weight: the first weight rule that enters, else the constant.
    std::uint16_t weightRule = kAnimNoRule;
    float weight = inputs.ConstantWeight;
    for (std::size_t i = 0; i < selector.WeightRules.size(); ++i)
    {
        const AnimBoundWeightRule& rule = selector.WeightRules[i];
        if (!EvaluateAnimProgram(rule.Enter, inputs.Predicate).Passed)
            continue;
        weightRule = static_cast<std::uint16_t>(i);
        const std::span<const std::uint32_t> facts = inputs.Predicate.Facts;
        weight = rule.FactSlot < 0 ? rule.Value
            : static_cast<std::size_t>(rule.FactSlot) < facts.size()
            ? std::clamp(AnimFactToFloat(facts[static_cast<std::size_t>(rule.FactSlot)]), 0.0f, 1.0f)
            : 0.0f;
        break;
    }
    outcome.WeightRuleChanged = weightRule != state.WeightRule;
    state.WeightRule = weightRule;
    state.Weight = std::isfinite(weight) ? weight : 0.0f;

    // The earliest tick a timer alone could change this layer's outcome.
    state.WakeTick = kAnimNoTick;
    if (state.HoldUntilTick > now)
        state.WakeTick = state.HoldUntilTick;
    for (const AnimTick until : state.CooldownUntil)
        if (until > now)
            state.WakeTick = std::min(state.WakeTick, until);
    return outcome;
}

void LogAnimSelection(AnimDecisionLog& log, AnimTick now, std::uint8_t layer, const AnimSelectionOutcome& outcome,
                      const AnimLayerSelection& state)
{
    AnimDecisionRecord record;
    record.Tick = now;
    record.Layer = layer;
    record.Rule = state.Winner;
    record.PreviousRule = outcome.Previous;
    record.Behavior = state.Behavior;
    if (outcome.LatchReleased && !outcome.LatchInterrupted)
    {
        AnimDecisionRecord released = record;
        released.Cause = AnimDecisionCause::LatchReleased;
        released.Reason = AnimChangeReason::LatchComplete;
        released.Rule = outcome.Changed ? outcome.Previous : state.Winner;
        log.Append(released);
    }
    if (outcome.LatchInterrupted)
    {
        AnimDecisionRecord interrupted = record;
        interrupted.Cause = AnimDecisionCause::LatchInterrupted;
        interrupted.Reason = AnimChangeReason::LatchInterrupted;
        log.Append(interrupted);
    }
    if (outcome.Changed)
    {
        record.Cause = AnimDecisionCause::WinnerChanged;
        record.Reason = outcome.Reason;
        log.Append(record);
    }
    if (outcome.LatchArmed)
    {
        record.Cause = AnimDecisionCause::LatchArmed;
        record.Reason = outcome.Reason;
        record.Request = state.LatchRequest;
        log.Append(record);
    }
    if (outcome.WeightRuleChanged)
    {
        AnimDecisionRecord weighted;
        weighted.Tick = now;
        weighted.Layer = layer;
        weighted.Cause = AnimDecisionCause::WeightChanged;
        weighted.Rule = state.WeightRule;
        weighted.Behavior = state.Behavior;
        log.Append(weighted);
    }
}

void SelectAnimEntity(const World& world, EntityId entity, const AnimBoundRig& rig,
                      std::span<const std::uint32_t> facts, AnimSelectorState& state, AnimTick now,
                      double tickSeconds, AnimDecisionLog* log,
                      std::vector<std::vector<AnimRuleVerdict>>* verdicts)
{
    const AnimRequestSet* requests =
        world.IsRegistered<AnimRequestSet>() ? world.TryGet<AnimRequestSet>(entity) : nullptr;
    const AnimContentState* content =
        world.IsRegistered<AnimContentState>() ? world.TryGet<AnimContentState>(entity) : nullptr;
    const GameplayTagContainer* tags =
        world.IsRegistered<GameplayTagContainer>() ? world.TryGet<GameplayTagContainer>(entity) : nullptr;

    // A prediction the authority decided otherwise leaves latches resting on
    // it. Selection starts again from the authority's word, as a joiner's
    // does, and the content pass rebuilds from what it picks.
    if (content != nullptr && content->Reconstruct)
    {
        const std::uint64_t generation = state.BindingGeneration;
        state = AnimSelectorState{};
        state.BindingGeneration = generation;
    }

    // A rebind can reorder or remove rules. The winner follows its stable
    // key; one that no longer exists resets, and says so.
    if (state.BindingGeneration != rig.Generation)
    {
        for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
        {
            AnimLayerSelection& layer = state.Layers[l];
            const int selectorIndex = rig.Layers[l].Selector;
            std::uint16_t remapped = kAnimNoRule;
            if (selectorIndex >= 0 && layer.Winner != kAnimNoRule)
            {
                const std::vector<AnimBoundRule>& rules = rig.Selectors[static_cast<std::size_t>(selectorIndex)].Rules;
                for (std::size_t r = 0; r < rules.size(); ++r)
                    if (rules[r].Key == layer.WinnerKey)
                        remapped = static_cast<std::uint16_t>(r);
            }
            if (layer.Winner != kAnimNoRule && remapped == kAnimNoRule)
            {
                if (log != nullptr)
                {
                    AnimDecisionRecord record;
                    record.Tick = now;
                    record.Cause = AnimDecisionCause::Anchored;
                    record.Reason = AnimChangeReason::Rebound;
                    record.Layer = static_cast<std::uint8_t>(l);
                    record.PreviousRule = layer.Winner;
                    record.Behavior = layer.Behavior;
                    log->Append(record);
                }
                layer = AnimLayerSelection{};
            }
            else
            {
                layer.Winner = remapped;
                layer.Previous = kAnimNoRule;
                // Slots are assigned per binding; expiries from another one
                // would land on the wrong rules.
                std::fill(std::begin(layer.CooldownUntil), std::end(layer.CooldownUntil), AnimTick{ 0 });
            }
        }
        state.BindingGeneration = rig.Generation;
    }

    const std::uint64_t requestDigest = RequestDigest(requests, now);
    const bool requestsChanged = !state.Evaluated || requestDigest != state.RequestDigest;
    state.FactDigest = FactDigest(facts, content);
    state.RequestDigest = requestDigest;
    state.Evaluated = true;

    for (std::size_t l = 0; l < rig.Layers.size() && l < kAnimMaxLayers; ++l)
    {
        const int selectorIndex = rig.Layers[l].Selector;
        if (selectorIndex < 0)
            continue;
        AnimLayerSelectInputs inputs;
        inputs.Predicate.Facts = facts;
        inputs.Predicate.Tags = tags;
        inputs.Predicate.Registry = world.TryGetResource<GameplayTagRegistry>();
        inputs.Predicate.Requests = requests;
        inputs.Predicate.Now = now;
        inputs.Predicate.TickSeconds = tickSeconds;
        inputs.Predicate.LayerBit = static_cast<std::uint8_t>(1u << l);
        inputs.ContentComplete = content != nullptr && content->Layers[l].ContentComplete;
        inputs.RequestsChanged = requestsChanged;
        inputs.ConstantWeight = rig.Layers[l].Weight;

        const AnimBoundSelector& selector = rig.Selectors[static_cast<std::size_t>(selectorIndex)];
        std::span<AnimRuleVerdict> layerVerdicts;
        if (verdicts != nullptr)
        {
            verdicts->resize(rig.Layers.size());
            (*verdicts)[l].resize(selector.Rules.size());
            layerVerdicts = (*verdicts)[l];
        }
        const AnimSelectionOutcome outcome = SelectAnimLayer(rig, selector, state.Layers[l], inputs, layerVerdicts);
        if (log != nullptr)
            LogAnimSelection(*log, now, static_cast<std::uint8_t>(l), outcome, state.Layers[l]);
    }
}

void AnimSelectSystem::FixedLogic(FixedLogicContext& ctx)
{
    SelectImpl(ctx.Entities, &ctx.Partitions, AuthorityTickOf(ctx.Entities, ctx.Time.TickIndex), ctx.Time.DeltaSeconds);
}

void AnimSelectSystem::Select(World& world, AnimTick now, double tickSeconds)
{
    SelectImpl(world, nullptr, now, tickSeconds);
}

void AnimSelectSystem::SelectImpl(World& world, const StoragePartitionSet* partitions, AnimTick now,
                                  double tickSeconds)
{
    EvaluatedCount = 0;
    SkippedCount = 0;
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>() || !world.IsRegistered<AnimSelectorState>())
        return;
    if (LastWorld != &world)
    {
        SmallQuery.reset();
        LargeQuery.reset();
        LastWorld = &world;
    }
    const bool hasLog = world.IsRegistered<AnimDecisionLog>();
    const bool hasContent = world.IsRegistered<AnimContentState>();

    const auto visit = [&]<typename Storage>(auto& view) {
        const auto rigs = view.template Read<AnimRig>();
        auto states = view.template Write<AnimSelectorState>();
        const auto facts = view.template Read<Storage>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = bindings->Resolve(rigs[i].Rig, world);
            if (rig == nullptr || !rig->Valid || rig->Selectors.empty())
                continue;
            const EntityId entity = view.Entity(i);
            const std::span<const std::uint32_t> values(facts[i].Values, std::min(rig->Slots.size(),
                                                                                    std::size(facts[i].Values)));
            AnimSelectorState& state = states[i];

            // Skip an entity nothing has touched: same facts, same requests,
            // same feedback, no timer due, nothing that reads the clock.
            bool timeDriven = false;
            AnimTick wake = kAnimNoTick;
            for (std::size_t l = 0; l < rig->Layers.size() && l < kAnimMaxLayers; ++l)
            {
                const int selector = rig->Layers[l].Selector;
                if (selector < 0)
                    continue;
                const AnimBoundSelector& bound = rig->Selectors[static_cast<std::size_t>(selector)];
                timeDriven = timeDriven || bound.ReadsTime || bound.ReadsTags
                    || state.Layers[l].Latch != AnimLatchState::None;
                wake = std::min(wake, state.Layers[l].WakeTick);
            }
            if (state.Evaluated && !timeDriven && now < wake && state.BindingGeneration == rig->Generation)
            {
                const World& reader = world;
                const AnimContentState* content = hasContent ? reader.TryGet<AnimContentState>(entity) : nullptr;
                const AnimRequestSet* requests = reader.TryGet<AnimRequestSet>(entity);
                if (FactDigest(values, content) == state.FactDigest && RequestDigest(requests, now) == state.RequestDigest)
                {
                    ++SkippedCount;
                    continue;
                }
            }

            AnimDecisionLog* log = hasLog ? world.TryGet<AnimDecisionLog>(entity) : nullptr;
            SelectAnimEntity(world, entity, *rig, values, state, now, tickSeconds, log);
            ++EvaluatedCount;
        }
    };
    const auto run = [&](auto& query, auto&& fn) {
        if (partitions != nullptr)
            query.ForEachChunkIn(*partitions, fn);
        else
            query.ForEachChunk(fn);
    };
    if (world.IsRegistered<AnimFacts>())
    {
        if (!SmallQuery.has_value())
            SmallQuery.emplace(world);
        run(*SmallQuery, [&](auto& view) { visit.template operator()<AnimFacts>(view); });
    }
    if (world.IsRegistered<AnimFactsLarge>())
    {
        if (!LargeQuery.has_value())
            LargeQuery.emplace(world);
        run(*LargeQuery, [&](auto& view) { visit.template operator()<AnimFactsLarge>(view); });
    }
}
