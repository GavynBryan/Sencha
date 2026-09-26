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

enum class AnimRuleVerdictKind : std::uint8_t
{
    // Not reached: a higher winner was chosen, or the winner stayed above it.
    NotEvaluated,
    Winner,
    Passed,
    Failed,
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
    // Winner only: it stayed (stay passed or latch held) rather than entering.
    bool Stayed = false;
    // The previous winner's stay was evaluated, and failed, rather than an enter.
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
    bool WeightRuleChanged = false;
};

struct AnimLayerSelectInputs
{
    AnimPredicateInputs Predicate;
    // The previous tick's feedback for this layer.
    bool ContentComplete = false;
    // The request set differs from the last evaluation.
    bool RequestsChanged = false;
    // The rig's constant weight for the layer, when no weight rule passes.
    float ConstantWeight = 1.0f;
};

// `verdicts`, when not empty, receives one verdict per flattened rule.
AnimSelectionOutcome SelectAnimLayer(const AnimBoundRig& rig,
                                     const AnimBoundSelector& selector,
                                     AnimLayerSelection& state,
                                     AnimLayerSelectInputs inputs,
                                     std::span<AnimRuleVerdict> verdicts = {});

void LogAnimSelection(AnimDecisionLog& log, AnimTick now, std::uint8_t layer,
                      const AnimSelectionOutcome& outcome, const AnimLayerSelection& state);

class AnimSelectSystem
{
public:
    explicit AnimSelectSystem(bool presentsPose = true) : PresentsPose(presentsPose) {}

    void FixedLogic(FixedLogicContext& ctx);
    void Select(World& world, AnimTick now, double tickSeconds);

    // Entities evaluated last pass, and those skipped because nothing they read changed.
    [[nodiscard]] std::size_t Evaluated() const { return EvaluatedCount; }
    [[nodiscard]] std::size_t Skipped() const { return SkippedCount; }

private:
    void SelectImpl(World& world, const StoragePartitionSet* partitions, AnimTick now, double tickSeconds);

    const World* LastWorld = nullptr;
    std::optional<Query<Read<AnimRig>, Write<AnimSelectorState>, Read<AnimFacts>>> SmallQuery;
    std::optional<Query<Read<AnimRig>, Write<AnimSelectorState>, Read<AnimFactsLarge>>> LargeQuery;
    std::size_t EvaluatedCount = 0;
    std::size_t SkippedCount = 0;
    bool PresentsPose = true;
};

// Shared with the preview; `verdicts` receives per-layer rule verdicts when given.
void SelectAnimEntity(const World& world, EntityId entity, const AnimBoundRig& rig,
                      std::span<const std::uint32_t> facts, AnimSelectorState& state, AnimTick now,
                      double tickSeconds, AnimDecisionLog* log,
                      std::vector<std::vector<AnimRuleVerdict>>* verdicts = nullptr);

// Ticks covering `ms` at the fixed step, rounded up so a hold never ends early.
[[nodiscard]] AnimTick AnimMsToTicks(float ms, double tickSeconds);
