#pragma once

#include <anim/AnimContentState.h>
#include <anim/AnimDecisionLog.h>
#include <anim/AnimFacts.h>
#include <anim/AnimPredicate.h>
#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <anim/AnimSelectorState.h>
#include <ecs/Query.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class StoragePartitionSet;
struct FixedLogicContext;

//=============================================================================
// Selection
//
// One layer, one tick: if the winner is latched its latch is its stay and only
// a rule the latch lets interrupt may replace it; otherwise the winner's stay
// is evaluated, and while it passes only higher rules are tried; when it fails
// every rule is tried top to bottom. A minimum hold keeps equal or lower bands
// out until it expires; a cooldown keeps a rule out for a while after it
// loses. First pass wins.
//
// SelectAnimLayer is the whole of it, pure over its arguments. The system runs
// it per entity; the editor runs it on a copy of the state to explain a tick,
// so what the debugger shows is what ran.
//=============================================================================

enum class AnimRuleVerdictKind : std::uint8_t
{
    // Not reached: a winner higher in the list was chosen first, or the
    // winner stayed and this rule ranks below it.
    NotEvaluated,
    Winner,
    // Evaluated and passed, but did not win.
    Passed,
    // Evaluated and failed at FailedRow.
    Failed,
    // In cooldown after losing.
    Cooldown,
    // Passed, but the winner's latch does not let this rule interrupt it.
    BlockedByLatch,
    // Would be tried, but the winner's minimum hold keeps its band out.
    BlockedByHold,
};

[[nodiscard]] std::string_view AnimRuleVerdictName(AnimRuleVerdictKind kind);

struct AnimRuleVerdict
{
    AnimRuleVerdictKind Kind = AnimRuleVerdictKind::NotEvaluated;
    // For the winner: whether it stayed (its stay passed or its latch held)
    // rather than entering this tick.
    bool Stayed = false;
    // The evaluation is of the rule's stay, not its enter: the winner of the
    // previous tick, which failed to stay.
    bool EvaluatedStay = false;
    // Failed: the first row that failed and what it compared.
    AnimPredicateResult Evaluation;
};

struct AnimSelectionOutcome
{
    bool Changed = false;
    AnimChangeReason Reason = AnimChangeReason::None;
    std::uint16_t Previous = kAnimNoRule;
    bool LatchArmed = false;
    bool LatchReleased = false;
    bool LatchInterrupted = false;
};

struct AnimLayerSelectInputs
{
    AnimPredicateInputs Predicate;
    // The previous tick's feedback for this layer.
    bool ContentComplete = false;
    // Whether the request set differs from the last evaluation, for the
    // reason a winner changed.
    bool RequestsChanged = false;
};

// `verdicts`, when not empty, receives one verdict per flattened rule.
AnimSelectionOutcome SelectAnimLayer(const AnimBoundRig& rig,
                                     const AnimBoundSelector& selector,
                                     AnimLayerSelection& state,
                                     AnimLayerSelectInputs inputs,
                                     std::span<AnimRuleVerdict> verdicts = {});

// Writes the records an outcome produces into `log`.
void LogAnimSelection(AnimDecisionLog& log, AnimTick now, std::uint8_t layer,
                      const AnimSelectionOutcome& outcome, const AnimLayerSelection& state);

class AnimSelectSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);
    void Select(World& world, AnimTick now, double tickSeconds);

    // Entities whose selectors ran last pass, and the ones skipped because
    // nothing they read changed; for the wakeup tests and the profiler.
    [[nodiscard]] std::size_t Evaluated() const { return EvaluatedCount; }
    [[nodiscard]] std::size_t Skipped() const { return SkippedCount; }

private:
    void SelectImpl(World& world, const StoragePartitionSet* partitions, AnimTick now, double tickSeconds);

    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Write<AnimSelectorState>, Read<AnimFacts>>> SmallQuery;
    std::optional<Query<Read<AnimRig>, Write<AnimSelectorState>, Read<AnimFactsLarge>>> LargeQuery;
    std::size_t EvaluatedCount = 0;
    std::size_t SkippedCount = 0;
};

// Selects every layer of one entity. The pure half of the system, shared with
// the preview; `verdicts`, when given, receives per-layer rule verdicts.
void SelectAnimEntity(const World& world, EntityId entity, const AnimBoundRig& rig,
                      std::span<const std::uint32_t> facts, AnimSelectorState& state, AnimTick now,
                      double tickSeconds, AnimDecisionLog* log,
                      std::vector<std::vector<AnimRuleVerdict>>* verdicts = nullptr);

// Ticks covering `ms` at the fixed step, rounded up so a hold never ends early.
[[nodiscard]] AnimTick AnimMsToTicks(float ms, double tickSeconds);
