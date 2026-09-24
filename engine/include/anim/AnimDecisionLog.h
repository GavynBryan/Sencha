#pragma once

#include <anim/AnimRequestSet.h>
#include <anim/AnimTypes.h>
#include <authored/VerbInvocation.h>
#include <ecs/ComponentAnnotations.h>

#include <cstdint>
#include <string_view>

//=============================================================================
// AnimDecisionLog
//
// A ring of what changed on an animated entity and why. Every state change the
// runtime makes writes one record with a cause, and a record with no cause is
// a bug: the invariants in docs/plans/animation-runtime.md are only
// enforceable because every change is attributable.
//
// Optional by construction: an entity carries a log only in dev builds or when
// opted in, and every writer takes a nullable log. What it holds is compact
// ids; an inspector resolves names against what is still loaded.
//=============================================================================

enum class AnimDecisionCause : std::uint8_t
{
    RequestAdded,
    RequestSuperseded,
    RequestDeduplicated,
    RequestCancelled,
    RequestExpired,
    RequestRejected,
    // A layer's winning rule changed; Reason says why.
    WinnerChanged,
    LatchArmed,
    LatchReleased,
    LatchInterrupted,
    // A layer's content changed; Reason says why.
    ContentChanged,
    // A rebind could not keep an index a layer held, and reset it.
    Anchored,
    // A layer's flow entered a section; Reason says how, Section and
    // PreviousSection which.
    SectionChanged,
    // A clip event's mark was crossed; EventOutcome says what came of it and
    // Admission what its binding answered.
    EventCrossed,
    // A layer entered or left Behavior, and the behavior declares a
    // lifecycle event; EventOutcome and Admission as for a crossing.
    BehaviorEntered,
    BehaviorExited,
    // A flow entered or left Section, and the flow declares a section
    // lifecycle event; EventOutcome and Admission as for a crossing.
    SectionEntered,
    SectionExited,
    // Another weight rule now weights the layer; Rule is its index among the
    // selector's weight rules, or none for the rig's constant.
    WeightChanged,
    // A change to what a layer plays was absorbed into its pose: Blend says
    // how and for how long, PreviousBehavior and Behavior the change, and
    // BlendOverridden whether a pairwise override chose it. BlendMagnitude is
    // the largest joint offset an inertialization began from.
    BlendApplied,
};

// What a crossed clip event led to.
enum class AnimEventOutcome : std::uint8_t
{
    // Offered to its binding; the record's Admission is the answer.
    Fired,
    // The mark was passed over by a skip in time rather than played through.
    Skipped,
    // The layer weight was under the event's threshold.
    BelowWeight,
};

[[nodiscard]] std::string_view AnimEventOutcomeName(AnimEventOutcome outcome);

// Why a winner or content changed. Every change has one.
enum class AnimChangeReason : std::uint8_t
{
    None,
    FactsChanged,
    RequestsChanged,
    HoldExpired,
    LatchComplete,
    LatchInterrupted,
    Rebound,
    BehaviorChanged,
    RowChanged,
    // Flow sections: entering the flow, following on, repeating, taking a
    // branch, going to the cancel section, starting from a request's anchor.
    FlowStarted,
    SectionFollowed,
    SectionLooped,
    SectionBranched,
    SectionCancelled,
    FlowAnchored,
    // The request driving a layer's content changed without its behavior
    // changing: a superseding request, a combo advancing.
    RequestSuperseded,
};

[[nodiscard]] std::string_view AnimChangeReasonName(AnimChangeReason reason);

[[nodiscard]] std::string_view AnimDecisionCauseName(AnimDecisionCause cause);

// Why a request was not accepted. Capacity is the one the architecture names:
// nothing is evicted, so server and client decide identically.
enum class AnimRejectReason : std::uint8_t
{
    None,
    Capacity,
    // The intent is not one the rig's request schema declares.
    UndeclaredIntent,
    // The request names no source, no intent, or no layer.
    Malformed,
};

[[nodiscard]] std::string_view AnimRejectReasonName(AnimRejectReason reason);

inline constexpr std::uint8_t kAnimNoLayer = 0xFF;

struct AnimDecisionRecord
{
    AnimTick Tick = 0;
    AnimDecisionCause Cause = AnimDecisionCause::RequestAdded;
    std::uint8_t Layer = kAnimNoLayer;
    AnimRequestId Request;
    GameplayTagId Intent;
    AnimCancelReason CancelReason = AnimCancelReason::None;
    AnimRejectReason RejectReason = AnimRejectReason::None;
    AnimChangeReason Reason = AnimChangeReason::None;
    // Winner and latch records: the flattened rules before and after.
    std::uint16_t Rule = kAnimNoRule;
    std::uint16_t PreviousRule = kAnimNoRule;
    GameplayTagId Behavior;
    // Content records: the row and content resolved.
    std::uint16_t Row = kAnimNoContent;
    std::uint16_t Content = kAnimNoContent;
    // Event records: which event of the content, what came of it, and its
    // binding's answer when it fired.
    std::uint32_t EventKey = 0;
    AnimEventOutcome EventOutcome = AnimEventOutcome::Fired;
    VerbAdmission Admission = VerbAdmission::Accepted;
    // Section records, and section lifecycle events: the flow's sections.
    std::uint8_t Section = 0xFF;
    std::uint8_t PreviousSection = 0xFF;
    // Blend records.
    GameplayTagId PreviousBehavior;
    float BlendSeconds = 0.0f;
    float BlendMagnitude = 0.0f;
    AnimBlendMode Blend = AnimBlendMode::Snap;
    bool BlendOverridden = false;
};

inline constexpr std::size_t kAnimDecisionLogCapacity = 64;

struct SENCHA_COMPONENT("sencha.anim_decision_log") AnimDecisionLog
{
    AnimDecisionRecord Records[kAnimDecisionLogCapacity] = {};
    // Records ever written. The newest is at (Written - 1) % capacity, and the
    // ring holds min(Written, capacity) of them.
    std::uint64_t Written = 0;

    void Append(const AnimDecisionRecord& record)
    {
        Records[Written % kAnimDecisionLogCapacity] = record;
        ++Written;
    }

    [[nodiscard]] std::size_t Size() const
    {
        return Written < kAnimDecisionLogCapacity ? static_cast<std::size_t>(Written)
                                                  : kAnimDecisionLogCapacity;
    }

    // Oldest first: index 0 is the oldest record still held.
    [[nodiscard]] const AnimDecisionRecord& At(std::size_t index) const
    {
        const std::uint64_t first = Written - Size();
        return Records[(first + index) % kAnimDecisionLogCapacity];
    }
};

#if !defined(SENCHA_CODEGEN)
#  include <anim/AnimDecisionLog.sencha.h>
#endif
